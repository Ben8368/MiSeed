"""Capture framed display, unfiltered kmsg and final FDT on the Windows host."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

if __package__:
    from .collect_charge_log import ROOT, ExportError, open_windows_port
    from .charge_log_windows import receive_windows
    from .check_runtime_dt import audit_report, decode_export, parse
else:
    from collect_charge_log import ROOT, ExportError, open_windows_port
    from charge_log_windows import receive_windows
    from check_runtime_dt import audit_report, decode_export, parse


class ControlError(ExportError):
    """The device returned a complete frame reporting a failed light request."""


def validate_frame(snapshot, kind):
    text = snapshot.replace(b"\r\n", b"\n")
    lines = text.splitlines()
    begin, end = kind + b"_BEGIN", kind + b"_END"
    if lines.count(begin) != 1 or lines.count(end) != 1:
        raise ExportError("missing/duplicated evidence frame: " + kind.decode())
    first, last = lines.index(begin), lines.index(end)
    if last <= first:
        raise ExportError("reordered evidence frame")
    frame = b"\n".join(lines[first:last + 1]) + b"\n"
    if kind + b"_ERROR" in frame:
        raise ExportError("device reported evidence collection failure")
    if kind == b"DISPLAY_CONTROL" and not re.search(rb" rc=0 ", frame):
        raise ControlError("backlight control failed")
    if kind == b"KMSG":
        header = re.search(rb"KMSG_SNAPSHOT bytes=(\d+) ring_bytes=(\d+) filtered=0 clear=0\n", frame)
        if not header or int(header[1]) > int(header[2]):
            raise ExportError("invalid kernel-log snapshot bounds")
        payload_end = frame.find(b"\nKMSG_NOTE ", header.end())
        if payload_end < 0 or payload_end - header.end() != int(header[1]):
            raise ExportError("kernel-log snapshot lost bytes")
    if kind == b"DISPLAY_VBLANK":
        vblank_result(frame)
    if kind == b"NFC_L3":
        rc = nfc_l3_result(frame)
        if not rc and not all(marker in lines[first:last] for marker in (
                b"NFC DEV_UP/reset-init passed.",
                b"NFC close/CORE_RESET/reopen/final close passed; shared VEN was retained."
        )):
            raise ExportError("NFC success frame lacks reset/init or reopen evidence")
        if not rc and not re.search(rb"^NFC RF discovery passed: targets=[1-8] protocol=\d+; UID omitted\.$",
                                    frame, re.M):
            raise ExportError("NFC success frame lacks target discovery evidence")
    return frame


def vblank_result(frame):
    result = re.findall(rb"^DISPLAY_VBLANK_RESULT events=(\d+) sequence_gaps=(\d+) .* rc=(-?\d+)$",
                        frame, re.MULTILINE)
    if len(result) != 1 or int(result[0][0]) > 91:
        raise ExportError("invalid or duplicated vblank result")
    return tuple(map(int, result[0]))


def nfc_l3_result(frame):
    lines = frame.splitlines()
    records = [line for line in lines if line.startswith(b'NFC_L3_RESULT')]
    if len(records) != 1 or lines[-2:] != [records[0], b'NFC_L3_END']:
        raise ExportError("missing, duplicated or misplaced NFC L3 result")
    match = re.fullmatch(rb'NFC_L3_RESULT rc=(0|-[1-9]\d{0,9})', records[0])
    if not match or int(match[1]) < -(1 << 31):
        raise ExportError("invalid NFC L3 result")
    return int(match[1])


def capture(port, output, command, kind, *, timeout=120):
    partial = output.with_suffix(output.suffix + ".partial")
    def validate(snapshot, stream, remaining):
        if kind == b"DTB":
            try:
                decode_export(snapshot.decode("ascii"))
            except ValueError as exc:
                raise ExportError(f"invalid DTB export: {exc}") from exc
        else:
            validate_frame(snapshot, kind)
        stream.write(snapshot)  # receive_windows preserves raw bytes itself on failure.
    with partial.open("xb") as stream:
        request = command.encode("ascii") + b"\n"
        if port.write(request) != len(request):
            raise ExportError("short console command write")
        receive_windows(port, stream, timeout, max_bytes=8 * 1024 * 1024,
                        end_mark=b"\n" + kind + b"_END", unavailable_mark=kind + b"_ERROR",
                        export_error=ExportError, validate=validate)
    partial.rename(output)


def capture_sequence(port, output, light_panel, measure_vblank=False):
    control_error = None
    capture(port, output / "display-before.txt", "display", b"DISPLAY")
    if measure_vblank:
        capture(port, output / "display-vblank.txt", "display-vblank",
                b"DISPLAY_VBLANK")
    if light_panel:
        try:
            capture(port, output / "display-control.txt", "display-on", b"DISPLAY_CONTROL")
        except ControlError as exc:
            # A completed failure frame still leaves the console responsive.
            # Preserve its partial and gather the logs needed to diagnose it.
            control_error = str(exc)
        capture(port, output / "display-after.txt", "display", b"DISPLAY")
    capture(port, output / "dmesg.txt", "dmesg", b"KMSG")
    capture(port, output / "runtime-fdt.txt", "dtb", b"DTB")
    return control_error


def prepare_output(path, parser):
    output = path.resolve()
    if os.name != "nt":
        parser.error("this capture entry point requires the Windows host")
    if not any(output.is_relative_to(ROOT / root) and output != ROOT / root
               for root in (".work", "local")) or output.exists():
        parser.error("output must be a new directory inside repository .work/ or local/")
    return output


def write_report(output, report):
    report["files"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted(output.iterdir()) if p.is_file()}
    (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if report["result"] == "FAIL" else 0


def display_report(output, reference, measure_vblank, light_panel,
                   capture_error=None, control_error=None):
    report = {"issues": [], "checks": {}}
    runtime_export = output / "runtime-fdt.txt"
    if runtime_export.exists():
        try:
            runtime = decode_export(runtime_export.read_text(encoding="ascii"))
            (output / "runtime-fdt.dtb").write_bytes(runtime)
            report = audit_report(reference, runtime)
        except (OSError, ValueError) as exc:
            report["issues"].append({"section": "runtime_fdt", "message": str(exc)})
    issues, checks = report["issues"], report["checks"]
    checks["capture"] = "FAIL" if capture_error else "PASS"
    checks["display_vblank"] = "NOT_RUN" if not measure_vblank else "INCOMPLETE"
    checks["physical_panel_hz"] = "NOT_MEASURED"
    checks["runtime_fdt"] = ("INCOMPLETE" if not runtime_export.exists() else
                             "FAIL" if issues else "PASS")
    if capture_error:
        issues.append({"section": "capture", "message": capture_error})
    if control_error:
        issues.append({"section": "display_control", "message": control_error})
    checks["display_control"] = ("FAIL" if control_error else "NOT_RUN" if not light_panel else
                                 "PASS" if (output / "display-control.txt").exists() else "INCOMPLETE")
    sample = output / "display-vblank.txt"
    if measure_vblank and sample.exists():
        events, gaps, rc = vblank_result(validate_frame(sample.read_bytes(), b"DISPLAY_VBLANK"))
        if rc or gaps or events != 91:
            issues.append({"section": "display_vblank",
                           "message": f"incomplete callback sample: events={events}, gaps={gaps}, rc={rc}"})
            checks["display_vblank"] = "FAIL"
        else:
            checks["display_vblank"] = "PASS"
    report["result"] = "FAIL" if issues or "INCOMPLETE" in checks.values() else "PASS scoped evidence"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--light-panel", action="store_true",
                        help="explicitly set the unique CSOT backlight to 1500 via standard sysfs")
    parser.add_argument("--measure-vblank", action="store_true",
                        help="collect 91 read-only DRM vblank waits from CRTC 0")
    args = parser.parse_args()
    output = prepare_output(args.output, parser)
    # Validate all offline inputs before opening the device.
    reference = args.reference.read_bytes()
    # Packaged memory/reg is populated by ABL, so it is not a runtime RAM sample.
    parse(reference)
    output.mkdir(parents=True)
    control_error = capture_error = None
    try:
        port = open_windows_port(args.port)
        try:
            control_error = capture_sequence(port, output, args.light_panel,
                                             args.measure_vblank)
        finally:
            port.close()
    except (ExportError, OSError) as exc:
        capture_error = str(exc)
    return write_report(output, display_report(output, reference, args.measure_vblank,
                                                args.light_panel, capture_error, control_error))


if __name__ == "__main__":
    raise SystemExit(main())
