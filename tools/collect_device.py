"""Collect allowlisted Android metadata via adb; never boot or write the phone."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
from datetime import datetime, timezone
import json
import re
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
TIMEOUT_SECONDS = 10
PROPERTIES = (
    "ro.product.device",
    "ro.product.model",
    "ro.board.platform",
    "ro.build.fingerprint",
    "ro.build.version.release",
    "ro.lineage.version",
    "ro.boot.bootloader",
    "gsm.version.baseband",
    "ro.boot.slot_suffix",
    "ro.boot.flash.locked",
    "ro.boot.verifiedbootstate",
)
PARTITIONS = ("shell", "ls", "-l", "/dev/block/by-name")
QUERIES = (("get-state",), *(("shell", "getprop", name) for name in PROPERTIES),
           PARTITIONS)
# Opt-in only. pdo_h and src_cap_id are cached getters in the pinned LOS
# policy_engine.c; get_src_cap_ext triggers PD traffic and is NOT allowed.
POWER_PROPERTIES = ("ro.boot.hwversion", "ro.boot.hardware.sku", "ro.product.board")
POWER_QUERIES = (
    *(("shell", "getprop", name) for name in POWER_PROPERTIES),
    ("shell", "uname", "-r"),
    ("shell", "dmesg"),
    ("shell", "cat", "/sys/class/usbpd/usbpd0/src_cap_id"),
    ("shell", "cat", "/sys/class/usbpd/usbpd0/pdo_h"),
)
PUMP_ID = re.compile(r"\[bq2597x\]\s+bq2597x_detect_device:\s*detect device:\s*([0-9]{1,3})\b")


def collect_power(adb, serial):
    observations = {name: query(adb, serial, ("shell", "getprop", name))
                    for name in POWER_PROPERTIES}
    observations["kernel_release"] = query(adb, serial, ("shell", "uname", "-r"))
    log = query(adb, serial, ("shell", "dmesg"))
    if log["status"] == "ok":
        ids = sorted({int(v) for v in PUMP_ID.findall(log["value"]) if int(v) <= 255})
        # Never retain raw kernel logs, unrelated messages or device identifiers.
        log = {"status": "ok" if len(ids) == 1 else "unavailable",
               "value": ids or None, "evidence": "boot_log_register_0x13_decimal",
               "qualification": "observation_only_not_board_calibration"}
    observations["pump_ids"] = log
    cap = ("shell", "cat", "/sys/class/usbpd/usbpd0/src_cap_id")
    before = query(adb, serial, cap)
    pdos = query(adb, serial, ("shell", "cat", "/sys/class/usbpd/usbpd0/pdo_h"))
    after = query(adb, serial, cap)
    observations["source_cap_id_before"] = before
    observations["source_cap_id_after"] = after
    observations["source_pdos"] = pdos
    if before["status"] != "ok" or after != before:
        pdos["status"] = "unavailable"
        pdos["error"] = "source_capabilities_changed_or_unavailable"
    return observations


UNRESOLVED = (
    "panel_and_touch_ids",
    "firmware_partition_hashes",
    "boot_image_and_memory_contract",
    "partition_sizes_and_identities",
    "backup_and_rollback_plan",
    "power_and_thermal_protection",
)


class CollectionError(ValueError):
    """Failure before a usable identity report can be collected."""


def query(adb, serial, command):
    """Commands are fixed locally, including the arguments passed to adb shell."""
    if command not in QUERIES + POWER_QUERIES:
        raise CollectionError("command is outside the read-only allowlist")
    try:
        result = subprocess.run(
            [adb, "-s", serial, *command], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=TIMEOUT_SECONDS,
            check=False, shell=False,
        )
    except subprocess.TimeoutExpired:
        return {"status": "error", "value": None, "error": "timeout"}
    except OSError:
        return {"status": "error", "value": None, "error": "adb_unavailable"}
    # adb errors may contain a serial/address. Never copy raw stderr to reports.
    if result.returncode:
        return {"status": "error", "value": None, "error": "adb_exit",
                "returncode": result.returncode}
    value = result.stdout.strip()
    return {"status": "ok" if value else "unavailable", "value": value or None}


def collect_identity(adb, serial, *, power=False):
    if (not serial or serial.startswith("-") or not serial.isprintable()
            or any(c.isspace() for c in serial)):
        raise CollectionError("provide one explicit adb serial/address")
    state = query(adb, serial, ("get-state",))
    if state["status"] != "ok" or state["value"] != "device":
        raise CollectionError("selected device is unavailable; check adb authorization/state")
    identity = query(adb, serial, ("shell", "getprop", PROPERTIES[0]))
    if identity["status"] != "ok" or identity["value"] != "umi":
        raise CollectionError("ro.product.device must report umi; stop for manual review")
    observations = {PROPERTIES[0]: identity}
    for name in PROPERTIES[1:]:
        observations[name] = query(adb, serial, ("shell", "getprop", name))
    observations["partition_links"] = query(adb, serial, PARTITIONS)
    if power:
        observations.update(collect_power(adb, serial))
    complete = all(item["status"] == "ok" for item in observations.values())
    return {
        "schema_version": 1,
        "collected_at_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "android_read_only_metadata",
        "collection_status": "complete" if complete else "partial",
        "validation": "device_reported_only",
        "observations": observations,
        "unresolved": list(UNRESOLVED),
    }


def report_path(root, output):
    """Keep raw reports in the ignored local directory, without overwriting."""
    root = root.resolve()
    path = output if output.is_absolute() else root / output
    path = path.resolve()
    if not path.is_relative_to(root / "local") or path.suffix != ".json":
        raise CollectionError("output must be a JSON file under this repository's local/")
    if path.exists():
        raise CollectionError("output exists; choose a new filename")
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--power", action="store_true",
                        help="read cached LOS pump-ID logs and PD capabilities; no PD requests")
    parser.add_argument("--serial", required=True, help="explicit device from adb devices")
    parser.add_argument("--output", type=Path, default=Path("local/device-identity.json"))
    args = parser.parse_args(argv)
    try:
        output = report_path(ROOT, args.output)
        adb = shutil.which("adb")
        if adb is None:
            raise CollectionError("adb not found; install official Android SDK Platform Tools")
        report = collect_identity(adb, args.serial, power=args.power)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
    except (CollectionError, OSError) as error:
        # No subprocess exception or raw error is printed: it may include identifiers.
        # CollectionError strings are locally defined and safe to show.
        detail = str(error) if isinstance(error, CollectionError) else "cannot save report"
        print("ERROR:", detail)
        return 2
    print(f"{report['collection_status'].upper()}: {output}")
    print("Device-reported metadata only; boot compatibility and recovery remain unverified.")
    return 0 if report["collection_status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
