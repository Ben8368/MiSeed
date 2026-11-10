"""Verify an extracted Linux cache against a pinned Git tree (offline)."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import hashlib
import json
import os
import stat
import subprocess
from pathlib import Path

if __package__:
    from . import apply_pd_diagnostics as pd
    from .apply_power_port import RPMHPD_KCONFIG_RULE
else:
    import apply_pd_diagnostics as pd
    from apply_power_port import RPMHPD_KCONFIG_RULE

MAKEFILE = "arch/arm64/boot/dts/qcom/Makefile"
GENERATED = {
    "Documentation/devicetree/bindings/power/supply/qcom,pm8150b-fg.yaml",
    "Documentation/devicetree/bindings/power/supply/qcom,pm8150b-charger.yaml",
    "Documentation/devicetree/bindings/power/supply/xiaomi,umi-bq25970.yaml",
    ".miseed-source-revision",
    ".miseed-pd-diagnostics.json",
    "arch/arm64/boot/dts/qcom/umi-csot.dts",
    "arch/arm64/boot/dts/qcom/umi-csot-abl-anchors.dtsi",
    "drivers/mfd/miseed_power_observer.c",
    "drivers/power/supply/qcom_fg.c",
    "drivers/power/supply/qcom_pm8150b_charger.c",
    "drivers/power/supply/miseed-power.Kconfig",
    "drivers/power/supply/umi_charge_policy.h",
    "arch/arm64/boot/dts/qcom/umi-pm8150b.dtsi",
    "arch/arm64/boot/dts/qcom/umi-pps.dtsi",
    "drivers/power/supply/umi_pps.c",
    "drivers/power/supply/umi_pps_policy.h",
    "drivers/power/supply/umi_smb5.h",
}
OVERLAY_RULES = {
    MAKEFILE: [(b"\n", b"dtb-y += umi-csot.dtb"),
               (b"", b"DTC_FLAGS_umi-csot += -@")],
    "drivers/mfd/Makefile": [
        (b"\n", b"obj-$(CONFIG_MFD_SPMI_PMIC) += miseed_power_observer.o")],
    "drivers/power/supply/Makefile": [
        (b"\n", b"obj-$(CONFIG_BATTERY_MISEED_QCOM_FG) += qcom_fg.o"),
        (b"\n", b"obj-$(CONFIG_CHARGER_MISEED_PM8150B) += qcom_pm8150b_charger.o"),
        (b"\n", b"obj-$(CONFIG_CHARGER_MISEED_UMI_PPS) += umi_pps.o")],
    "drivers/power/supply/Kconfig": [
        (b"\n", b'source "drivers/power/supply/miseed-power.Kconfig"')],
}


def git(repository, *args):
    return subprocess.check_output(["git", "-C", str(repository), *args])


def blob_hash(data):
    return hashlib.sha1(
        b"blob " + str(len(data)).encode("ascii") + b"\0" + data,
        usedforsecurity=False,
    ).hexdigest()


def verify_source(repository, revision, source):
    """Reject changed, missing, or extra inputs; allow only known MiSeed overlays."""
    source = Path(source)
    if source.is_symlink() or not source.is_dir():
        raise ValueError("cached source is not a regular directory")
    marker = source / ".miseed-source-revision"
    if not stat.S_ISREG(marker.lstat().st_mode):
        raise ValueError("cached Linux source revision marker is not a regular file")
    if marker.read_text().strip() != revision:
        raise ValueError("cached Linux source revision marker mismatch")
    if git(repository, "rev-parse", revision + "^{commit}").decode().strip() != revision:
        raise ValueError("Linux revision mismatch")
    patched_pd = {}
    pd_marker = source / ".miseed-pd-diagnostics.json"
    if pd_marker.exists() or pd_marker.is_symlink():
        receive = (Path(__file__).resolve().parents[1] / "kernel/power/pmic_pdphy_receive.inc").read_text(encoding="utf-8")
        for name, expected in pd.ORIGINAL.items():
            original = git(repository, "show", revision + ":" + name)
            if pd.digest(original) != expected:
                raise ValueError(f"PD baseline mismatch: {name}")
            patched_pd[name] = pd.transform(name, original.decode("utf-8"), receive).encode("utf-8")
            if pd.regular_path(source, name).read_bytes() != patched_pd[name]:
                raise ValueError(f"cached PD overlay changed: {name}")
        if json.loads(pd.regular_path(source, ".miseed-pd-diagnostics.json").read_text()) != pd.manifest_for(patched_pd, receive):
            raise ValueError("cached PD diagnostic manifest changed")
    tracked = set()
    records = git(repository, "ls-tree", "-rz", "--full-tree", revision)
    for record in records.split(b"\0"):
        if not record:
            continue
        metadata, raw_name = record.split(b"\t", 1)
        mode, kind, oid = metadata.decode("ascii").split()
        name = os.fsdecode(raw_name)
        tracked.add(name)
        path = source / name
        info = path.lstat()
        if kind != "blob":
            raise ValueError(f"unsupported Git input: {name}")
        if mode == "120000":
            if not stat.S_ISLNK(info.st_mode):
                raise ValueError(f"changed symlink type: {name}")
            actual = blob_hash(os.fsencode(os.readlink(path)))
        elif mode in {"100644", "100755"} and stat.S_ISREG(info.st_mode):
            if os.name != "nt" and bool(info.st_mode & 0o111) != (mode == "100755"):
                raise ValueError(f"changed executable mode: {name}")
            digest = hashlib.sha1(usedforsecurity=False)
            digest.update(b"blob " + str(info.st_size).encode("ascii") + b"\0")
            with path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            actual = digest.hexdigest()
            if name in patched_pd and path.read_bytes() == patched_pd[name]:
                continue
            if actual != oid and name == "drivers/pmdomain/qcom/Kconfig":
                original = git(repository, "show", revision + ":" + name).decode("utf-8")
                base = RPMHPD_KCONFIG_RULE.removesuffix("\n\tselect PM_OPP")
                if original.count(base) == 1 and path.read_bytes() == original.replace(base, RPMHPD_KCONFIG_RULE, 1).encode("utf-8"):
                    continue
            if actual != oid and name in OVERLAY_RULES:
                original = git(repository, "show", revision + ":" + name)
                expected = original
                for prefix, rule in OVERLAY_RULES[name]:
                    if rule not in expected.splitlines():
                        expected += prefix + rule + b"\n"
                if path.read_bytes() == expected:
                    continue
        else:
            raise ValueError(f"changed file type: {name}")
        if actual != oid:
            raise ValueError(f"cached Linux source differs from pinned commit: {name}")

    def walk_error(error):
        raise error

    for directory, dirs, files in os.walk(source, onerror=walk_error):
        for name in files + dirs:
            path = Path(directory) / name
            relative = path.relative_to(source).as_posix()
            if path.is_dir() and not path.is_symlink() and relative not in GENERATED:
                continue
            if relative not in tracked and relative not in GENERATED:
                raise ValueError(f"unexpected cached source input: {relative}")
            if relative in GENERATED and not stat.S_ISREG(path.lstat().st_mode):
                raise ValueError(f"generated input is not a regular file: {relative}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    try:
        verify_source(args.repository, args.revision, args.source)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"source cache verification failed: {error}\n")
    print(f"Cached Linux source verified against {args.revision}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
