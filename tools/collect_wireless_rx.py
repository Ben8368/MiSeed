"""Read the known Umi LineageOS wireless RX baseline over USB or paired ADB."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
from contextlib import nullcontext
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

if __package__:
    from .collect_dtbo import ROOT, adb_read, root_access
else:
    from collect_dtbo import ROOT, adb_read, root_access


KERNEL = b"4.19.325-cip131-st15-perf-g71b13e62f057"
USB_BASELINE = "usb-baseline"
WIRELESS_STAGES = ("off-pad", "on-pad", "after-removal")
POWER_GOOD = "wireless/wireless_power_good_en"
REQUIRED = (POWER_GOOD, "rx1619/present", "usb/present", "dc/present",
            "battery/status")
RX_TELEMETRY = (
    "wireless/input_voltage_regulation", "wireless/input_voltage_vrect", "wireless/rx_iout",
)
STATUS_FIELDS = REQUIRED + (
    "wireless/wireless_version", "wireless/tx_adapter", "wireless/wireless_cp_en",
    "usb/online", "usb/input_current_now", "usb/typec_mode", "dc/online",
    "battery/capacity", "battery/temp", "battery/current_now", "battery/voltage_now",
)

INVENTORY = r"""
for node in /sys/bus/i2c/devices/*-003b /sys/bus/i2c/devices/*-0061 /sys/bus/i2c/devices/*-0067; do
    [ -d "$node" ] || continue
    driver=$(readlink "$node/driver")
    printf '%s\t%s\n' "${node##*/}" "${driver##*/}"
done
"""
SAMPLE = r"""
read_field() {
    value=$(cat "/sys/class/power_supply/$field" 2>/dev/null) || value=
    printf '%s\t%s\n' "$field" "$value"
}
for field in "STATUS_FIELDS"; do
    read_field
    if [ "$field" = "POWER_GOOD" ]; then power_good=$value; fi
done
for field in "RX_TELEMETRY"; do
    if [ "$power_good" = 1 ]; then
        read_field
    else
        printf '%s\t\n' "$field"
    fi
done
""".replace('"STATUS_FIELDS"', " ".join(STATUS_FIELDS)).replace("POWER_GOOD", POWER_GOOD).replace(
    '"RX_TELEMETRY"', " ".join(RX_TELEMETRY))


def output_path(output):
    path = output.resolve()
    if (path.suffix != ".json" or path.exists()
            or not any(path.is_relative_to(ROOT / folder) for folder in (".work", "local"))):
        raise ValueError("output must be a new .json file under .work/ or local/")
    return path


def capture_id(adb, serial):
    boot_id = adb_read(adb, serial, "exec-out", "cat", "/proc/sys/kernel/random/boot_id").strip()
    if not re.fullmatch(rb"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}", boot_id):
        raise ValueError("Android boot ID is unavailable or malformed")
    return hashlib.sha256(boot_id).hexdigest()


def parse_rows(data, key_pattern):
    rows = {}
    for line in data.decode("ascii").splitlines():
        key, separator, value = line.partition("\t")
        if not separator or not re.fullmatch(key_pattern, key) or key in rows:
            raise ValueError("unexpected or duplicate readback field")
        if value and not re.fullmatch(r"[A-Za-z0-9_ +./:()-]{1,64}", value):
            raise ValueError(f"malformed readback value for {key}")
        rows[key] = value or None
    return rows


def snapshot(adb, serial):
    data = parse_rows(adb_read(adb, serial, "shell", SAMPLE), r"[a-z0-9_]+/[a-z0-9_]+")
    if data.keys() != set(STATUS_FIELDS + RX_TELEMETRY) or any(data[field] is None for field in REQUIRED):
        raise ValueError("required RX readbacks unavailable; check access and matching LOS kernel")
    if any(data[field] not in ("0", "1") for field in REQUIRED[:-1]):
        raise ValueError("invalid RX presence readback")
    return {"utc": datetime.now(timezone.utc).isoformat(), "fields": data}


def collect(adb, serial, stage, temporary_root):
    if adb_read(adb, serial, "shell", "getprop", "ro.product.device").strip() != b"umi":
        raise ValueError("connected Android device is not umi")
    if adb_read(adb, serial, "shell", "uname", "-r").strip() != KERNEL:
        raise ValueError("LOS source does not match the reviewed getter allowlist")
    version = adb_read(adb, serial, "shell", "getprop", "ro.lineage.version").decode("ascii").strip()
    if not re.fullmatch(r"[A-Za-z0-9.-]+-umi", version):
        raise ValueError("LOS version is unavailable or not for umi")
    before = adb_read(adb, serial, "exec-out", "id", "-u").strip()
    if before not in (b"0", b"2000"):
        raise ValueError("unexpected initial ADB privilege identity")
    access = root_access(adb, serial, True) if temporary_root else nullcontext(
        (False, "ordinary_adb" if before == b"2000" else "existing_adbd_root", before))
    with access as (_, method, initial_uid):
        if initial_uid != before:
            raise ValueError("ADB identity changed before capture")
        boot = capture_id(adb, serial)
        bindings = parse_rows(adb_read(adb, serial, "shell", INVENTORY), r"[0-9]+-00(?:3b|61|67)")
        rx_nodes = [key for key, driver in bindings.items() if key.endswith("-0061") and driver == "rx1619"]
        if len(rx_nodes) != 1 or any(driver for key, driver in bindings.items() if key.endswith("-003b")):
            raise ValueError("expected reviewed RX1619 binding")
        samples = [snapshot(adb, serial)]
        for _ in range(2):
            time.sleep(1)
            samples.append(snapshot(adb, serial))
        if capture_id(adb, serial) != boot:
            raise ValueError("Android rebooted during capture")
    after = adb_read(adb, serial, "exec-out", "id", "-u").strip()
    if after != before:
        raise ValueError("ADB privilege identity changed during capture")
    return {"stage": stage, "lineage_version": version, "kernel": KERNEL.decode("ascii"),
            "boot_id_sha256": boot, "bindings": bindings, "adb_access": method,
            "uid_before": int(before), "uid_after": int(after), "samples": samples}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True, type=Path)
    parser.add_argument("--wifi-serial", help="already paired Android Wireless debugging transport")
    parser.add_argument("--stage", choices=(USB_BASELINE, *WIRELESS_STAGES), required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--temporary-adb-root", action="store_true",
                        help="temporarily elevate and restore adbd on the USB transport")
    args = parser.parse_args(argv)
    if args.wifi_serial and args.temporary_adb_root:
        parser.error("switch adbd privilege over USB; restarting adbd changes the Wi-Fi endpoint")
    try:
        path = output_path(args.output)
        adb = args.adb.resolve()
        if args.stage == USB_BASELINE and args.wifi_serial is None:
            serial = subprocess.check_output([str(adb), "-d", "get-serialno"],
                                             stderr=subprocess.PIPE, timeout=4).decode().strip()
        elif args.stage in WIRELESS_STAGES and args.wifi_serial:
            serial = args.wifi_serial
            if not re.fullmatch(r"(?:[0-9.]+:[0-9]+|adb-[A-Za-z0-9-]+\._adb-tls-connect\._tcp\.?)",
                                serial):
                raise ValueError("wireless stages require a paired Wi-Fi transport address")
        else:
            raise ValueError("use USB for the tethered baseline and paired Wi-Fi for wireless stages")
        report = collect(adb, serial, args.stage, args.temporary_adb_root)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, indent=2)
            stream.write("\n")
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        detail = str(exc) if type(exc) is ValueError else type(exc).__name__
        parser.exit(1, "Wireless RX collection stopped: " + detail + "\n")
    print(f"Saved {args.stage} snapshot to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
