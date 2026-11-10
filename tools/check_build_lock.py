"""Check the reviewed Boot L2 record offline; never select or download inputs."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import hashlib
import json
import re
import stat
from pathlib import Path, PurePosixPath


PROFILE = "umi-csot-boot-l2"
MANIFEST = "manifests/sources.lock.json"
SCRIPT = "tools/build_kernel_l2.sh"
REQUIRED_INPUTS = {
    SCRIPT, "tools/check_build_lock.py", "tools/verify_kernel_source.py",
    "tools/download_locked.py", "tools/extract_abl_anchors.py",
    "tools/extract_boot_v2_dtb.py", "tools/isolate_abl_targets.py",
    "tools/apply_power_observer.py", "tools/apply_power_port.py",
    "tools/apply_pd_diagnostics.py", "tools/make_initramfs.py",
    "tools/apply_nfc_port.py",
    "tools/pack_android_boot_v2.py", "tests/test_abl_isolation.py",
    "kernel/umi-csot.dts", "kernel/umi-csot-dtbo-targets.txt",
    "kernel/umi-l2.config", "kernel/rescue_init.c", "kernel/charge_log.h",
    "kernel/power_observer.c", "kernel/power/qcom_fg.c",
    "kernel/power/qcom_pm8150b_charger.c", "kernel/power/Kconfig",
    "kernel/power/umi_charge_policy.h", "kernel/power/umi-pm8150b.dtsi",
    "kernel/power/umi_pps.c", "kernel/power/umi_pps_policy.h",
    "kernel/power/umi_smb5.h", "kernel/power/umi-pps.dtsi",
    "kernel/power/pmic_pdphy_receive.inc",
    "kernel/nfc/sn100x.c", "kernel/nfc/Kconfig",
    "kernel/nfc/bindings/nxp,sn100t.yaml",
    "LICENSES/BSD-3-Clause.txt", "LICENSES/GPL-2.0-only.txt",
}


def read_input(root, name):
    if (not isinstance(name, str) or not name or "\\" in name
            or ":" in name or PurePosixPath(name).is_absolute()
            or any(part in {"", ".", ".."} for part in name.split("/"))):
        raise ValueError(f"invalid repository input path: {name!r}")
    path = root
    for part in name.split("/"):
        path = path / part
        if path.is_symlink():
            raise ValueError(f"symlink input: {name}")
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError(f"not a regular input: {name}")
    return path.read_bytes()


def unique_objects(items, label):
    if not isinstance(items, list) or not items:
        raise ValueError(f"{label}: expected nonempty list")
    result = {}
    for item in items:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            raise ValueError(f"{label}: missing id")
        identity = item["id"]
        if not re.fullmatch(r"[a-z0-9-]+", identity) or identity in result:
            raise ValueError(f"{label}: invalid/duplicate id {identity!r}")
        result[identity] = item
    return result


def check_build_lock(root, actual=None):
    """Validate this profile only; this is not a recursive dependency audit."""
    root = Path(root).resolve()
    errors = []
    try:
        data = json.loads(read_input(root, MANIFEST))
        if not isinstance(data, dict) or type(data.get("schema_version")) is not int or data["schema_version"] != 1:
            raise ValueError("unsupported lock schema_version")
        profiles = unique_objects(data.get("build_profiles"), "build_profiles")
        sources = unique_objects(data.get("sources"), "sources")
        profile = profiles[PROFILE]
        resolved = {}
        for field in ("kernel_source", "device_reference", "board_support_reference"):
            identity, revision = profile[field].split("@")
            source = sources[identity]
            if (not re.fullmatch(r"[0-9a-f]{40}", revision)
                    or source.get("revision") != revision
                    or not isinstance(source.get("url"), str)
                    or not re.fullmatch(r"https://[^\s]+", source["url"])
                    or not isinstance(source.get("license"), str) or not source["license"].strip()
                    or not isinstance(source.get("submodules"), list)
                    or not isinstance(source.get("patches"), list)):
                raise ValueError(f"invalid reviewed source identity: {field}")
            resolved[field] = source
        template = profile["boot_header_template"]
        if (not isinstance(template, dict) or not isinstance(template.get("url"), str)
                or not re.fullmatch(r"https://[^\s]+", template["url"])
                or not isinstance(template.get("sha256"), str)
                or not re.fullmatch(r"[0-9a-f]{64}", template["sha256"])):
            raise ValueError("invalid boot_header_template")
        expected = {
            "kernel_url": resolved["kernel_source"]["url"],
            "kernel_revision": resolved["kernel_source"]["revision"],
            "reference_revision": resolved["device_reference"]["revision"],
            "boot_url": template["url"], "boot_sha256": template["sha256"],
        }
        script = read_input(root, SCRIPT).decode("utf-8")
        for key, value in expected.items():
            values = re.findall(r'^readonly ' + key + r'="([^"\n]+)"$', script, re.M)
            if values != [value]:
                errors.append(f"build script/lock mismatch: {key}")
            if actual is not None and actual.get(key) != value:
                errors.append(f"executing build/lock mismatch: {key}")
        nfc_reference = profile.get("nfc_driver_reference")
        if not isinstance(nfc_reference, str) or "@" not in nfc_reference:
            raise ValueError("invalid nfc_driver_reference")
        if nfc_reference != profile["kernel_source"]:
            raise ValueError("NFC transport must reference the pinned Linux NCI source")
        inputs = profile["project_inputs"]
        hashes = profile["input_sha256"]
        if (not isinstance(inputs, dict) or not inputs
                or any(not isinstance(v, str) for v in inputs.values())
                or len(set(inputs.values())) != len(inputs)
                or not isinstance(hashes, dict)):
            raise ValueError("invalid project_inputs/input_sha256")
        paths = set(inputs.values())
        if not REQUIRED_INPUTS.issubset(paths) or set(hashes) != paths:
            errors.append("build input inventory/hash coverage mismatch")
        host = profile.get("host_tools", {})
        if not isinstance(host, dict):
            raise ValueError("invalid host_tools")
        for inventory in (hashes, host):
            for name, digest in inventory.items():
                if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
                    raise ValueError(f"invalid input SHA-256: {name}")
                if hashlib.sha256(read_input(root, name)).hexdigest() != digest:
                    errors.append(f"locked input SHA-256 mismatch: {name}")
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        errors.append(f"build lock invalid: {error}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    keys = ("kernel_url", "kernel_revision", "reference_revision", "boot_url", "boot_sha256")
    for key in keys:
        parser.add_argument("--" + key.replace("_", "-"))
    args = parser.parse_args()
    actual = {key: getattr(args, key) for key in keys}
    if any(value is not None for value in actual.values()):
        if any(value is None for value in actual.values()):
            parser.error("all executing build identity arguments are required together")
    else:
        actual = None
    errors = check_build_lock(args.root, actual)
    for error in errors:
        print("ERROR:", error)
    if errors:
        return 1
    print("PASS: reviewed Boot L2 identity and local hashes (dependency closure remains open)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
