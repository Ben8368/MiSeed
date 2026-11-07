"""Export a retained MiSeed charge timeline over a reconnected USB serial port."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import os
from pathlib import Path
import re
import select
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
BEGIN = re.compile(rb"^CHARGE_LOG_BEGIN boot_id=\S+ first=(\d+) next=(\d+) count=(\d+) overwritten=(\d+) ram_only=1$")
END = re.compile(rb"^CHARGE_LOG_END next=(\d+)$")
RECORD = re.compile(rb"^seq=(\d+) boot_ms=\d+ ")
MAX_BYTES = 16 * 1024 * 1024


class ExportError(ValueError):
    pass


def receive(fd, stream, timeout):
    """Only a complete, contiguous BEGIN..END snapshot counts as success."""
    deadline = time.monotonic() + timeout
    pending = b""
    received = 0
    bounds = None
    expected = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if not select.select([fd], [], [], max(0, min(remaining, 0.2)))[0]:
            continue
        try:
            data = os.read(fd, 65536)
        except BlockingIOError:
            continue
        except OSError as error:
            raise ExportError("USB disconnected during export; reconnect and retry") from error
        if not data:
            raise ExportError("USB serial port closed before export completed")
        received += len(data)
        if received > MAX_BYTES:
            raise ExportError("export exceeded the 16 MiB receive limit")
        pending += data
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            line = line.rstrip(b"\r")
            if bounds is None:
                match = BEGIN.fullmatch(line)
                if not match:
                    if line.startswith(b"CHARGE_LOG_UNAVAILABLE"):
                        raise ExportError(line.decode("ascii", errors="replace"))
                    continue
                first, next_sequence, count, overwritten = map(int, match.groups())
                if next_sequence < first or next_sequence - first != count:
                    raise ExportError("invalid snapshot sequence bounds")
                bounds = (first, next_sequence, count, overwritten)
                expected = first
                stream.write(line + b"\n")
                continue
            stream.write(line + b"\n")
            record = RECORD.match(line)
            if record:
                if int(record.group(1)) != expected or expected >= bounds[1]:
                    raise ExportError("missing, duplicated or reordered log record")
                expected += 1
            end = END.fullmatch(line)
            if end:
                if int(end.group(1)) != bounds[1] or expected != bounds[1]:
                    raise ExportError("incomplete snapshot; reconnect and retry")
                return bounds
    raise ExportError("export timed out; RAM log is retained on the running device")


def output_path(output):
    path = output.resolve()
    if not any(path.is_relative_to(ROOT / root) for root in ("local", ".work")):
        raise ExportError("output must be under this repository's local/ or .work/")
    if path.suffix != ".log":
        raise ExportError("output must end in .log")
    if path.exists() or path.with_suffix(".log.partial").exists():
        raise ExportError("output or partial file already exists; choose a new filename")
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit /dev/cu.usbmodem... or /dev/ttyACM...")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(argv)
    fd = None
    partial = None
    try:
        if sys.platform not in ("darwin", "linux"):
            raise ExportError("this collector requires macOS or Linux; use charge-log in a serial terminal on Windows")
        import termios
        import tty

        if not 0 < args.timeout <= 600:
            raise ExportError("timeout must be greater than zero and at most 600 seconds")
        if not args.port.startswith(("/dev/cu.usbmodem", "/dev/ttyACM")):
            raise ExportError("select one explicit USB CDC-ACM serial port")
        output = output_path(args.output)
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(fd)
        settings = termios.tcgetattr(fd)
        settings[4] = settings[5] = termios.B115200
        settings[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        termios.tcflush(fd, termios.TCIFLUSH)
        command = memoryview(b"charge-log\n")
        while command:
            try:
                written = os.write(fd, command)
            except BlockingIOError:
                if not select.select([], [fd], [], 2)[1]:
                    raise ExportError("USB command write timed out")
                continue
            if written <= 0:
                raise ExportError("USB command write failed")
            command = command[written:]
        output.parent.mkdir(parents=True, exist_ok=True)
        partial = output.with_suffix(".log.partial")
        with partial.open("xb") as stream:
            bounds = receive(fd, stream, args.timeout)
        # Atomic, exclusive publication; a failed/racing export never overwrites.
        os.link(partial, output)
        partial.unlink()
        print(f"SAVED: {output}")
        print(f"Records: {bounds[2]}; overwritten older records: {bounds[3]}; RAM log remains available.")
        return 0
    except (ExportError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        if partial and partial.exists():
            print(f"Incomplete export retained: {partial}", file=sys.stderr)
        return 1
    finally:
        if fd is not None:
            os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
