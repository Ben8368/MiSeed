"""Export a retained MiSeed charge timeline over a reconnected USB serial port."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import io
import os
from pathlib import Path
import re
import select
import sys
import time

if __package__:
    from .charge_log_windows import WINDOWS_RX_BUFFER, receive_windows as _receive_windows
else:
    from charge_log_windows import WINDOWS_RX_BUFFER, receive_windows as _receive_windows


ROOT = Path(__file__).resolve().parents[1]
BEGIN = re.compile(rb"^CHARGE_LOG_BEGIN boot_id=\S+ first=(\d+) next=(\d+) count=(\d+) (?:overwritten=(\d+)|storage_errors=(\d+)) ram_only=1$")
END = re.compile(rb"^CHARGE_LOG_END next=(\d+)$")
RECORD = re.compile(rb"^seq=(\d+) boot_ms=\d+ ")
MAX_BYTES = 256 * 1024 * 1024
END_MARK = b"\nCHARGE_LOG_END next="
UNAVAILABLE_MARK = b"CHARGE_LOG_UNAVAILABLE"


class ExportError(ValueError):
    pass


def receive(fd, stream, timeout):
    def read_chunk(remaining):
        if not select.select([fd], [], [], max(0, min(remaining, 0.2)))[0]:
            return None
        return os.read(fd, 65536)

    return receive_chunks(read_chunk, stream, timeout)


def receive_chunks(read_chunk, stream, timeout):
    """Only a complete, contiguous BEGIN..END snapshot counts as success."""
    deadline = time.monotonic() + timeout
    pending = b""
    received = 0
    bounds = None
    expected = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        try:
            data = read_chunk(remaining)
        except BlockingIOError:
            continue
        except OSError as error:
            raise ExportError("USB disconnected during export; reconnect and retry") from error
        if data is None:
            continue
        if not data:
            raise ExportError("USB serial port closed before export completed")
        received += len(data)
        if received > MAX_BYTES:
            raise ExportError("export exceeded the 256 MiB receive limit")
        lines = (pending + data).split(b"\n")
        pending = lines.pop()
        for line in lines:
            line = line.rstrip(b"\r")
            if bounds is None:
                match = BEGIN.fullmatch(line)
                if not match:
                    if line.startswith(b"CHARGE_LOG_UNAVAILABLE"):
                        raise ExportError(line.decode("ascii", errors="replace"))
                    continue
                first, next_sequence, count = map(int, match.groups()[:3])
                overwritten = int(match.group(4) or b"0")
                storage_errors = int(match.group(5) or b"0")
                if next_sequence < first or next_sequence - first != count:
                    raise ExportError("invalid snapshot sequence bounds")
                bounds = (first, next_sequence, count, overwritten, storage_errors)
                expected = first
                stream.write(line + b"\n")
                continue
            stream.write(line + b"\n")
            record = RECORD.match(line)
            if record:
                if int(record.group(1)) != expected or expected >= bounds[1]:
                    raise ExportError("missing, duplicated or reordered log record")
                expected += 1
                continue
            end = END.fullmatch(line)
            if end:
                if int(end.group(1)) != bounds[1] or expected != bounds[1]:
                    raise ExportError("incomplete snapshot; reconnect and retry")
                return bounds
            raise ExportError("malformed line inside charge snapshot")
    raise ExportError("export timed out; RAM log is retained on the running device")



def _validate_buffered_snapshot(data, stream, timeout):
    chunks = iter((bytes(data),))
    validated = io.BytesIO()
    bounds = receive_chunks(lambda remaining: next(chunks, b""), validated, max(1, timeout))
    stream.write(validated.getvalue())
    return bounds


def receive_buffered(read_chunk, stream, timeout):
    """Drain USB before parsing/writing, bounded by the same time/size limits.

    The Windows live test dropped records while parsing each incoming chunk.
    Buffer raw bytes through the footer, then validate the complete snapshot.
    A failure retains all received raw bytes for diagnosis, never a final log.
    """
    deadline = time.monotonic() + timeout
    data = bytearray()
    scan = 0
    unavailable_scan = 0
    try:
        while time.monotonic() < deadline:
            try:
                chunk = read_chunk(deadline - time.monotonic())
            except BlockingIOError:
                continue
            except OSError as error:
                raise ExportError("USB disconnected during export; reconnect and retry") from error
            if chunk is None:
                continue
            if not chunk:
                raise ExportError("USB serial port closed before export completed")
            if len(data) + len(chunk) > MAX_BYTES:
                data.extend(chunk[:MAX_BYTES - len(data)])
                raise ExportError("export exceeded the 256 MiB receive limit")
            data.extend(chunk)
            if data.find(UNAVAILABLE_MARK, unavailable_scan) >= 0:
                raise ExportError("charge log unavailable; raw response retained")
            unavailable_scan = max(0, len(data) - len(UNAVAILABLE_MARK) + 1)
            # Only inspect newly arrived bytes plus a possible split footer.
            marker = data.find(END_MARK, scan)
            if marker >= 0:
                end = data.find(b"\n", marker + 1)
                if end >= 0:
                    return _validate_buffered_snapshot(data, stream,
                                                       deadline - time.monotonic())
                scan = marker
            else:
                scan = max(0, len(data) - len(END_MARK))
        raise ExportError("export timed out; RAM log is retained on the running device")
    except (ExportError, OSError):
        stream.write(data)
        raise


def receive_windows(serial_port, stream, timeout):
    return _receive_windows(
        serial_port, stream, timeout, max_bytes=MAX_BYTES, end_mark=END_MARK,
        unavailable_mark=UNAVAILABLE_MARK, export_error=ExportError,
        validate=_validate_buffered_snapshot)


def output_path(output):
    path = output.resolve()
    if not any(path.is_relative_to(ROOT / root) for root in ("local", ".work")):
        raise ExportError("output must be under this repository's local/ or .work/")
    if path.suffix != ".log":
        raise ExportError("output must end in .log")
    if path.exists() or path.with_suffix(".log.partial").exists():
        raise ExportError("output or partial file already exists; choose a new filename")
    return path


def open_windows_port(name):
    if not re.fullmatch(r"COM[1-9][0-9]*", name, re.IGNORECASE):
        raise ExportError("select one explicit COM port")
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as error:
        raise ExportError("Windows export requires the optional pyserial package") from error
    matches = [p for p in list_ports.comports()
               if p.device.upper() == name.upper() and p.vid == 0x1d6b and p.pid == 0x0104]
    if len(matches) != 1:
        raise ExportError("COM port is not the unique expected MiSeed diagnostic interface")
    port = serial.Serial(name, 115200, timeout=0.05, write_timeout=2)
    try:
        # Prefer a large Win32 COM RX queue; ignore drivers that reject the API.
        try:
            port.set_buffer_size(rx_size=WINDOWS_RX_BUFFER, tx_size=4096)
        except Exception:
            try:
                port.set_buffer_size(rx_size=1024 * 1024, tx_size=4096)
            except Exception:
                pass
        port.dtr = True
        port.reset_input_buffer()
        command = b"help" + bytes((10,))
        if port.write(command) != len(command):
            raise ExportError("USB identity command write failed")
        greeting = bytearray()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            greeting.extend(port.read(65536))
            if len(greeting) > 65536:
                raise ExportError("unexpected console greeting size")
            if greeting.endswith(b"miseed> "):
                if b"MiSeed Umi/CSOT USB diagnostic." not in greeting:
                    raise ExportError("unexpected USB console identity")
                return port
        raise ExportError("USB console identity check timed out")
    except Exception:
        port.close()
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True,
                        help="explicit COMn, /dev/cu.usbmodem... or /dev/ttyACM...")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(argv)
    fd = None
    serial_port = None
    partial = None
    try:
        if not 0 < args.timeout <= 600:
            raise ExportError("timeout must be greater than zero and at most 600 seconds")
        output = output_path(args.output)
        if sys.platform == "win32":
            serial_port = open_windows_port(args.port)
        elif sys.platform in ("darwin", "linux"):
            import termios
            import tty

            if not args.port.startswith(("/dev/cu.usbmodem", "/dev/ttyACM")):
                raise ExportError("select one explicit USB CDC-ACM serial port")
            fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            tty.setraw(fd)
            settings = termios.tcgetattr(fd)
            settings[4] = settings[5] = termios.B115200
            settings[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(fd, termios.TCSANOW, settings)
            termios.tcflush(fd, termios.TCIFLUSH)
        else:
            raise ExportError("this collector supports Windows, macOS and Linux")
        output.parent.mkdir(parents=True, exist_ok=True)
        partial = output.with_suffix(".log.partial")
        with partial.open("xb") as stream:
            command = memoryview(b"charge-log" + bytes((10,)))
            while command:
                try:
                    written = serial_port.write(command) if serial_port is not None else os.write(fd, command)
                except BlockingIOError:
                    if not select.select([], [fd], [], 2)[1]:
                        raise ExportError("USB command write timed out")
                    continue
                if written <= 0:
                    raise ExportError("USB command write failed")
                command = command[written:]
            if serial_port is not None:
                bounds = receive_windows(serial_port, stream, args.timeout)
            else:
                bounds = receive(fd, stream, args.timeout)
        # Atomic, exclusive publication; a failed/racing export never overwrites.
        os.link(partial, output)
        partial.unlink()
        print(f"SAVED: {output}")
        print(f"Records: {bounds[2]}; overwritten older records: {bounds[3]}; "
              f"storage errors: {bounds[4]}; RAM log remains available.")
        return 0
    except (ExportError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        if partial and partial.exists():
            print(f"Incomplete export retained: {partial}", file=sys.stderr)
        return 1
    finally:
        if serial_port is not None:
            serial_port.close()
        if fd is not None:
            os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
