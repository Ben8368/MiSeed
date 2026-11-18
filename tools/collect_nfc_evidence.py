"""One-shot Windows capture of standard NFC/NCI L3; no retries or VEN reset."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
from pathlib import Path
import re

if __package__:
    from .collect_display_evidence import (ExportError, capture, nfc_l3_result,
                                          open_windows_port, prepare_output, write_report)
else:
    from collect_display_evidence import (ExportError, capture, nfc_l3_result,
                                         open_windows_port, prepare_output, write_report)


def check_identity(frame):
    bound = re.findall(rb"^NFC I2C device=(\d+-0028) driver=sn100x$", frame, re.M)
    if not (len(bound) == 1 and b"NFC DT matches=1 scan=finished" in frame
            and b"NFC class devices=1 (" in frame and b"truncated" not in frame):
        raise ExportError("expected one bound SN100T and one NFC controller; no RF command sent")


def collect(port, output, report):
    status = output / "nfc-status.txt"
    capture(port, status, "nfc-status", b"NFC_STATUS")
    check_identity(status.read_bytes().replace(b"\r", b""))
    probe = output / "nfc-l3.txt"
    capture(port, probe, "nfc-l3", b"NFC_L3", timeout=120)
    report["probe_rc"] = nfc_l3_result(probe.read_bytes())
    if report["probe_rc"]:
        report["issues"].append({"section": "nfc_l3", "message": "NCI/RF/reopen probe failed"})
    # Only send another command after observing a complete probe frame.
    capture(port, output / "dmesg.txt", "dmesg", b"KMSG")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--run-rf", action="store_true", required=True,
                        help="explicitly run one authorized RF probe with a non-payment test tag")
    args = parser.parse_args()
    output = prepare_output(args.output, parser)
    output.mkdir(parents=True)
    report = {"probe_rc": None, "issues": []}
    try:
        port = open_windows_port(args.port)
        try:
            collect(port, output, report)
        finally:
            port.close()
    except (ExportError, OSError) as exc:
        report["issues"].append({"section": "capture", "message": str(exc)})
    report["result"] = "FAIL" if report["issues"] else "PASS scoped NCI/RF/reopen"
    return write_report(output, report)


if __name__ == "__main__":
    raise SystemExit(main())
