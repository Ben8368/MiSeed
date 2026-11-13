"""Read only the connected Umi DTBO via an already authorized Android su shell.

An explicit flag permits temporary adbd root with identity restoration.
No phone reboot, remount, device file creation, partition or power writes.
"""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

if __package__:
    from .dtbo_table import MAX_IMAGE, inspect, selected_indices, table_entries
else:
    from dtbo_table import MAX_IMAGE, inspect, selected_indices, table_entries

ROOT = Path(__file__).resolve().parents[1]


def adb_read(adb, serial, *command, timeout=15):
    result = subprocess.run([str(adb), "-s", serial, *command], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=timeout)
    if result.returncode:
        raise ValueError("ADB read failed: " + " ".join(command[:2]))
    return result.stdout


def wait_uid(adb, serial, expected, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if adb_read(adb, serial, "exec-out", "id", "-u", timeout=2).strip() == expected:
                return
        except (ValueError, subprocess.SubprocessError):
            pass
        time.sleep(0.2)
    raise ValueError("ADB did not reconnect with the expected privilege identity")


@contextmanager
def root_access(adb, serial, temporary_adb_root):
    before = adb_read(adb, serial, "exec-out", "id", "-u").strip()
    if before not in (b"0", b"2000"):
        raise ValueError("unexpected initial ADB privilege identity")
    attempted = False
    try:
        if before == b"0":
            yield False, "existing_adbd_root", before
        elif temporary_adb_root:
            # Mark before sending: even a transport timeout must try restoration.
            attempted = True
            adb_read(adb, serial, "root")
            wait_uid(adb, serial, b"0")
            if adb_read(adb, serial, "shell", "getprop", "ro.product.device").strip() != b"umi":
                raise ValueError("device identity changed after ADB reconnection")
            yield False, "temporary_adbd_root", before
        else:
            if adb_read(adb, serial, "exec-out", "su", "-c", "id -u").strip() != b"0":
                raise ValueError("no existing root read access; ordinary ADB cannot read DTBO")
            yield True, "existing_su", before
    finally:
        if attempted:
            try:
                adb_read(adb, serial, "unroot")
                wait_uid(adb, serial, before)
            except (OSError, ValueError, subprocess.SubprocessError) as exc:
                raise ValueError("could not verify restoration of ordinary ADB; inspect device") from exc


def read_command(use_su, *tokens):
    # Tokens are fixed read-only commands/paths in this module, never user text.
    return ("exec-out", "su", "-c", " ".join(tokens)) if use_su else ("exec-out", *tokens)


def collect(adb, output, temporary_adb_root=False):
    repo_root = ROOT.resolve()
    output = output.resolve()
    if output.exists() or not any(output.is_relative_to(repo_root / n) for n in (".work", "local")):
        raise ValueError("output must be a new directory under repository .work/ or local/")
    devices = subprocess.run([str(adb), "devices"], capture_output=True, text=True,
                             timeout=10, check=True).stdout.splitlines()
    rows = [line.split() for line in devices[1:] if line.strip()]
    if len(rows) != 1 or len(rows[0]) != 2 or rows[0][1] != "device":
        raise ValueError("one already authorized Android ADB device is required")
    serial = rows[0][0]  # Use the real enumeration privately; never print or store it.
    if adb_read(adb, serial, "shell", "getprop", "ro.product.device").strip() != b"umi":
        raise ValueError("connected Android device is not umi")
    selection = adb_read(adb, serial, "shell", "getprop", "ro.boot.dtbo_idx").decode("ascii").strip()
    indices = selected_indices(selection)
    with root_access(adb, serial, temporary_adb_root) as (use_su, method, before):
        report = capture(adb, serial, output, selection, indices, use_su)
    # No success file until a temporary elevation was positively restored.
    report["adb_access"] = method
    report["adb_uid_before"] = before.decode("ascii")
    report["adb_uid_after"] = adb_read(adb, serial, "exec-out", "id", "-u").decode("ascii").strip()
    if report["adb_uid_after"] != report["adb_uid_before"]:
        raise ValueError("ADB privilege identity changed during capture")
    (output / "dtbo.img.partial").rename(output / "dtbo.img")
    (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def capture(adb, serial, output, selection, indices, use_su):
    boot_id = adb_read(adb, serial, *read_command(use_su, "cat", "/proc/sys/kernel/random/boot_id")).strip()
    if not re.fullmatch(rb"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}", boot_id):
        raise ValueError("Android boot ID is unavailable or malformed")
    output.mkdir(parents=True)
    partial = output / "dtbo.img.partial"
    # Bound the block read; all bytes go to the host. No device output file.
    with partial.open("xb") as stream:
        result = subprocess.run([str(adb), "-s", serial, *read_command(use_su, "head", "-c", "67108865",
                                                                         "/dev/block/by-name/dtbo")],
                                stdout=stream, stderr=subprocess.PIPE, timeout=45)
    if result.returncode or partial.stat().st_size > MAX_IMAGE:
        raise ValueError("DTBO read failed or exceeded size limit; host partial retained")
    blob = partial.read_bytes()
    table_entries(blob)
    root = {}
    for prop in ("compatible", "model", "qcom,board-id", "qcom,msm-id"):
        value = adb_read(adb, serial, *read_command(use_su, "cat",
                         "/sys/firmware/devicetree/base/" + prop))
        if prop in ("model", "compatible"):
            if not value.endswith(b"\0") or not value[:-1] or b"\0\0" in value:
                raise ValueError("root string property unavailable or malformed: " + prop)
        elif not value or len(value) % 8:
            raise ValueError("root ID property unavailable or malformed: " + prop)
        root[prop] = value
        (output / (prop + ".bin")).write_bytes(value)
    after = adb_read(adb, serial, "shell", "getprop", "ro.boot.dtbo_idx").decode("ascii").strip()
    if after != selection or adb_read(adb, serial, *read_command(use_su, "cat",
                                     "/proc/sys/kernel/random/boot_id")).strip() != boot_id:
        raise ValueError("Android boot/DTBO selection changed during capture")
    report = inspect(blob, indices, root)
    report["scope"] = "read-only DTBO captured from uniquely enumerated umi in one Android boot"
    report["boot_id_sha256"] = hashlib.sha256(boot_id).hexdigest()
    if not report["metadata_matches_runtime"]:
        report["result"] = "FAIL selected metadata differs from Android runtime root"
    else:
        report["result"] = "PASS read-only acquisition and selected root metadata comparison"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--temporary-adb-root", action="store_true",
                        help="explicitly permit temporary adbd root and restore its initial shell identity")
    args = parser.parse_args()
    try:
        report = collect(args.adb.resolve(), args.output, args.temporary_adb_root)
        print(json.dumps(report, indent=2))
        return 0 if report["result"].startswith("PASS") else 1
    except ValueError as exc:
        parser.exit(1, "Read-only DTBO acquisition stopped: " + str(exc) + "\n")
    except (OSError, subprocess.SubprocessError) as exc:
        # Exception text from ADB may contain a serial; expose only the class.
        parser.exit(1, "Read-only DTBO acquisition stopped: " + type(exc).__name__ + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
