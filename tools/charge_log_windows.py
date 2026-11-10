"""Windows CDC-ACM drain helpers for charge-log export."""

# SPDX-License-Identifier: AGPL-3.0-only

import threading
import time

WINDOWS_RX_BUFFER = 8 * 1024 * 1024


def receive_windows(serial_port, stream, timeout, *, max_bytes, end_mark, unavailable_mark,
                    export_error, validate):
    """Keep a dedicated Win32 reader draining CDC-ACM while waiting for END.

    Parsing and file writes stay off the reader thread so large snapshots do not
    stall ReadFile long enough for the host USB stack to drop bytes.
    """
    lock = threading.Lock()
    data = bytearray()
    errors = []
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            try:
                chunk = serial_port.read(256 * 1024)
            except OSError as error:
                errors.append(error)
                return
            except StopIteration:
                time.sleep(0.005)
                continue
            if chunk:
                with lock:
                    data.extend(chunk)

    thread = threading.Thread(target=reader, name="miseed-charge-log-rx", daemon=True)
    thread.start()
    deadline = time.monotonic() + timeout
    scan = 0
    unavailable_scan = 0
    try:
        while time.monotonic() < deadline:
            if errors:
                raise export_error(
                    "USB disconnected during export; reconnect and retry") from errors[0]
            with lock:
                if len(data) > max_bytes:
                    del data[max_bytes:]
                    raise export_error("export exceeded the 256 MiB receive limit")
                snapshot = bytes(data)
            if snapshot.find(unavailable_mark, unavailable_scan) >= 0:
                raise export_error("charge log unavailable; raw response retained")
            unavailable_scan = max(0, len(snapshot) - len(unavailable_mark) + 1)
            marker = snapshot.find(end_mark, scan)
            if marker >= 0:
                end = snapshot.find(b"\n", marker + 1)
                if end >= 0:
                    stop.set()
                    thread.join(timeout=2)
                    return validate(snapshot, stream, deadline - time.monotonic())
                scan = marker
            else:
                scan = max(0, len(snapshot) - len(end_mark))
            time.sleep(0.005)
        raise export_error("export timed out; RAM log is retained on the running device")
    except (export_error, OSError):
        with lock:
            stream.write(bytes(data))
        raise
    finally:
        stop.set()
        thread.join(timeout=2)
