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
    return frame


def capture(port, output, command, kind):
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
        receive_windows(port, stream, 120, max_bytes=8 * 1024 * 1024,
                        end_mark=b"\n" + kind + b"_END", unavailable_mark=kind + b"_ERROR",
                        export_error=ExportError, validate=validate)
    partial.rename(output)


def capture_sequence(port, output, light_panel):
    control_error = None
    capture(port, output / "display-before.txt", "display", b"DISPLAY")
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--light-panel", action="store_true",
                        help="explicitly set the unique CSOT backlight to 1500 via standard sysfs")
    args = parser.parse_args()
    output = args.output.resolve()
    if os.name != "nt":
        parser.error("this capture entry point requires the Windows host")
    if not any(output.is_relative_to(ROOT / root) for root in (".work", "local")) or output.exists():
        parser.error("output must be a new directory inside repository .work/ or local/")
    # Validate all offline inputs before opening the device.
    reference = args.reference.read_bytes()
    # Packaged memory/reg is populated by ABL, so it is not a runtime RAM sample.
    parse(reference)
    output.mkdir(parents=True)
    port = open_windows_port(args.port)
    try:
        control_error = capture_sequence(port, output, args.light_panel)
    finally:
        port.close()
    runtime = decode_export((output / "runtime-fdt.txt").read_text(encoding="ascii"))
    (output / "runtime-fdt.dtb").write_bytes(runtime)
    result = audit_report(reference, runtime)
    if control_error:
        result["issues"].append({"section": "display_control", "message": control_error})
        result["result"] = "FAIL"
    result["files"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted(output.iterdir()) if p.is_file()}
    (output / "evidence.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 1 if result["result"] == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
