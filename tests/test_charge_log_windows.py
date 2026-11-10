"""Cross-platform framing and Windows collector tests; no hardware access."""

# SPDX-License-Identifier: AGPL-3.0-only

import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from tools import collect_charge_log as collector


BEGIN = b"CHARGE_LOG_BEGIN boot_id=test first=0 next=2 count=2 overwritten=0 ram_only=1\n"
ROWS = b"seq=0 boot_ms=1 SAMPLE a\nseq=1 boot_ms=2 KERNEL b\n"
END = b"CHARGE_LOG_END next=2\n"
SNAPSHOT = BEGIN + ROWS + END


class ChunkTests(unittest.TestCase):
    def receive(self, chunks):
        chunks = iter(chunks)
        output = io.BytesIO()
        bounds = collector.receive_chunks(lambda remaining: next(chunks, b""), output, 1)
        return bounds, output.getvalue()

    def test_fragmented_lines_idle_reads_banner_and_prompt(self):
        wire = b"console banner\r\n" + SNAPSHOT.replace(b"\n", b"\r\n") + b"miseed> "
        chunks = [chunk for byte in wire for chunk in (None, bytes([byte]))]
        self.assertEqual(self.receive(chunks), ((0, 2, 2, 0, 0), SNAPSHOT))

    def test_accepts_growing_ram_log_header(self):
        header = b"CHARGE_LOG_BEGIN boot_id=test first=0 next=2 count=2 storage_errors=0 ram_only=1\n"
        snapshot = header + ROWS + END
        bounds, output = self.receive([snapshot])
        self.assertEqual(bounds, (0, 2, 2, 0, 0))
        self.assertEqual(output, snapshot)

    def test_invalid_frames_and_early_disconnect(self):
        cases = {
            "gap": BEGIN + ROWS.splitlines(keepends=True)[1] + END,
            "duplicate": BEGIN + ROWS + ROWS + END,
            "wrong_footer": BEGIN + ROWS + b"CHARGE_LOG_END next=3\n",
            "early_footer": BEGIN + END,
            "truncated": BEGIN + ROWS,
            "invalid_bounds": BEGIN.replace(b"count=2", b"count=3") + ROWS + END,
            "malformed_record": BEGIN + b"seq=0 truncated\n" + END,
            "unexpected_line": BEGIN + b"unexpected diagnostic\n" + ROWS + END,
            "unavailable": b"CHARGE_LOG_UNAVAILABLE recorder_not_started\n",
        }
        for name, data in cases.items():
            with self.subTest(name=name), self.assertRaises(collector.ExportError):
                self.receive([data])

    def test_receive_limit_and_timeout(self):
        with patch.object(collector, "MAX_BYTES", 10), self.assertRaisesRegex(collector.ExportError, "receive limit"):
            self.receive([SNAPSHOT])
        with patch.object(collector.time, "monotonic", side_effect=[0, 0, 0, 2]):
            with self.assertRaisesRegex(collector.ExportError, "timed out"):
                collector.receive_chunks(lambda remaining: None, io.BytesIO(), 1)

    def test_disconnect_is_reported_without_publishing(self):
        with self.assertRaisesRegex(collector.ExportError, "disconnected"):
            collector.receive_chunks(Mock(side_effect=OSError("removed")), io.BytesIO(), 1)



class BufferedTests(unittest.TestCase):
    def test_unavailable_is_detected_inside_large_or_fragmented_chunks(self):
        for chunks in ([b"banner\nCHARGE_LOG_UNAVAILABLE no_recorder\n" + b"x" * 400],
                       [b"banner\nCHARGE_LOG_UNAV", b"AILABLE no_recorder\n" + b"x" * 400]):
            chunks = iter(chunks)
            output = io.BytesIO()
            with self.assertRaisesRegex(collector.ExportError, "unavailable"):
                collector.receive_buffered(lambda remaining: next(chunks, b""), output, 1)
            self.assertIn(b"CHARGE_LOG_UNAVAILABLE", output.getvalue())

    def test_fragmented_footer_and_idle_reads_publish_only_after_receive(self):
        chunks = iter([None, b"banner\n", None] + [bytes([byte]) for byte in SNAPSHOT])
        output = io.BytesIO()
        def read_chunk(remaining):
            self.assertEqual(output.getvalue(), b"")
            return next(chunks, b"")
        bounds = collector.receive_buffered(read_chunk, output, 1)
        self.assertEqual(bounds, (0, 2, 2, 0, 0))
        self.assertEqual(output.getvalue(), SNAPSHOT)

    def test_corrupt_frame_preserves_full_raw_response_including_later_rows(self):
        raw = b"banner\n" + BEGIN + ROWS.replace(b"seq=0", b"seq=1") + END + b"miseed> "
        output = io.BytesIO()
        with self.assertRaises(collector.ExportError):
            collector.receive_buffered(lambda remaining: raw, output, 1)
        self.assertEqual(output.getvalue(), raw)

    def test_disconnect_and_timeout_preserve_received_bytes(self):
        for disconnect in (True, False):
            output = io.BytesIO()
            chunks = iter([BEGIN, OSError("removed") if disconnect else None])
            def read_chunk(remaining):
                value = next(chunks, None)
                if isinstance(value, Exception):
                    raise value
                return value
            clock = [0, 0, 0, 0, 0, 2]
            with self.subTest(disconnect=disconnect), patch.object(collector.time, "monotonic", side_effect=clock):
                with self.assertRaises(collector.ExportError):
                    collector.receive_buffered(read_chunk, output, 1)
            self.assertEqual(output.getvalue(), BEGIN)

    def test_receive_limit_retains_only_bounded_raw_bytes(self):
        output = io.BytesIO()
        with patch.object(collector, "MAX_BYTES", 10), self.assertRaisesRegex(collector.ExportError, "receive limit"):
            collector.receive_buffered(lambda remaining: b"x" * 11, output, 1)
        self.assertEqual(output.getvalue(), b"x" * 10)

    def test_large_snapshot_keeps_all_records(self):
        count = 9000
        raw = (f"CHARGE_LOG_BEGIN boot_id=test first=0 next={count} count={count} overwritten=0 ram_only=1\n".encode()
               + b"".join(f"seq={i} boot_ms={i} SAMPLE x\n".encode() for i in range(count))
               + f"CHARGE_LOG_END next={count}\n".encode())
        chunks = iter(raw[i:i+65536] for i in range(0, len(raw), 65536))
        output = io.BytesIO()
        self.assertEqual(collector.receive_buffered(lambda remaining: next(chunks,b""),output,2),
                         (0,count,count,0,0))
        self.assertEqual(output.getvalue(),raw)

    def test_receive_limit_covers_maximum_tcpmlog_sized_charge_snapshot(self):
        self.assertGreaterEqual(collector.MAX_BYTES, 262144 * 768)

    def test_windows_reader_thread_publishes_after_background_drain(self):
        port = Mock()
        wire = [b"banner\n", BEGIN, ROWS[:20], ROWS[20:], END]

        def read(_size):
            return wire.pop(0) if wire else b""

        port.read.side_effect = read
        output = io.BytesIO()
        bounds = collector.receive_windows(port, output, 2)
        self.assertEqual(bounds, (0, 2, 2, 0, 0))
        self.assertEqual(output.getvalue(), SNAPSHOT)

    def test_windows_reader_retains_raw_bytes_on_gap(self):
        port = Mock()
        raw = BEGIN + b"seq=1 boot_ms=1 SAMPLE gap\n" + END
        pending = [raw]

        def read(_size):
            return pending.pop(0) if pending else b""

        port.read.side_effect = read
        output = io.BytesIO()
        with self.assertRaises(collector.ExportError):
            collector.receive_windows(port, output, 2)
        self.assertEqual(output.getvalue(), raw)


class WindowsTests(unittest.TestCase):
    def port(self, data=SNAPSHOT):
        port = Mock()
        port.read.side_effect = [data]
        port.write.side_effect = lambda data: len(data)
        return port

    def invoke(self, root, port):
        output = root / ".work/session.log"
        with patch.object(collector, "ROOT", root), patch.object(collector.sys, "platform", "win32"):
            with patch.object(collector, "open_windows_port", return_value=port):
                with patch("sys.stdout", new_callable=io.StringIO), patch("sys.stderr", new_callable=io.StringIO):
                    result = collector.main(["--port", "COM6", "--output", str(output), "--timeout", "1"])
        return result, output

    def test_main_publishes_only_verified_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            port = self.port()
            pending = [SNAPSHOT]

            def read(_size):
                return pending.pop(0) if pending else b""

            port.read.side_effect = read
            result, output = self.invoke(Path(directory).resolve(), port)
            self.assertEqual(result, 0)
            self.assertEqual(output.read_bytes(), SNAPSHOT)
            self.assertFalse(output.with_suffix(".log.partial").exists())
            port.write.assert_called_once()
            self.assertEqual(bytes(port.write.call_args.args[0]), b"charge-log\n")
            port.close.assert_called_once()

    def test_main_retains_partial_on_sequence_gap_or_disconnect(self):
        for data in (BEGIN + b"seq=1 boot_ms=1 SAMPLE gap\n" + END, None):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as directory:
                port = self.port(data)
                if data is None:
                    port.read.side_effect = OSError("removed")
                else:
                    pending = [data]

                    def read(_size, pending=pending):
                        return pending.pop(0) if pending else b""

                    port.read.side_effect = read
                result, output = self.invoke(Path(directory).resolve(), port)
                self.assertEqual(result, 1)
                self.assertFalse(output.exists())
                self.assertTrue(output.with_suffix(".log.partial").exists())
                port.close.assert_called_once()

    def test_main_refuses_existing_output_or_partial_before_open(self):
        for suffix in (".log", ".log.partial"):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                saved = root / (".work/session" + suffix)
                saved.parent.mkdir()
                saved.write_bytes(b"prior evidence")
                port = self.port()
                result, _ = self.invoke(root, port)
                self.assertEqual(result, 1)
                self.assertEqual(saved.read_bytes(), b"prior evidence")
                port.write.assert_not_called()
                port.close.assert_not_called()

    def open_port(self, port, devices, name="COM6"):
        serial = Mock()
        serial.Serial.return_value = port
        tools = Mock()
        tools.list_ports.comports.return_value = devices
        with patch.dict("sys.modules", {"serial": serial, "serial.tools": tools}):
            return collector.open_windows_port(name), serial

    def test_identity_requires_vid_pid_and_greeting(self):
        device = SimpleNamespace(device="COM6", vid=0x1D6B, pid=0x0104)
        port = self.port(b"MiSeed Umi/CSOT USB diagnostic.\r\nmiseed> ")
        result, serial = self.open_port(port, [device], "com6")
        self.assertIs(result, port)
        serial.Serial.assert_called_once_with("com6", 115200, timeout=0.05, write_timeout=2)
        port.set_buffer_size.assert_called_once_with(
            rx_size=collector.WINDOWS_RX_BUFFER, tx_size=4096)
        port.write.assert_called_once_with(b"help\n")
        port.close.assert_not_called()
        for devices in ([], [device, device], [SimpleNamespace(device="COM6", vid=1, pid=2)]):
            with self.subTest(devices=devices), self.assertRaises(collector.ExportError):
                self.open_port(self.port(), devices)

    def test_wrong_greeting_short_write_and_invalid_port_fail_closed(self):
        device = SimpleNamespace(device="COM6", vid=0x1D6B, pid=0x0104)
        for short_write in (False, True):
            port = self.port(b"Other device\r\nmiseed> ")
            if short_write:
                port.write.side_effect = lambda data: 0
            with self.assertRaises(collector.ExportError):
                self.open_port(port, [device])
            port.close.assert_called_once()
        with self.assertRaisesRegex(collector.ExportError, "explicit COM"):
            collector.open_windows_port("auto")


if __name__ == "__main__":
    unittest.main()
