"""Synthetic backlight identity, write failures and non-clearing kernel capture."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import collect_display_evidence as collector
from tools.collect_display_evidence import validate_frame, ExportError

ROOT = Path(__file__).resolve().parents[1]


class EvidenceFrameTests(unittest.TestCase):
    def test_invalid_dtb_capture_retains_raw_response(self):
        frames = (b"DTB_BEGIN 40\nDTB 00000000 00\nDTB_END\n",
                  b"DTB_BEGIN 40\nDTB 00000000 " + b"00" * 40 + b"\nDTB_END\n",
                  b"DTB_BEGIN 40\n\xff\nDTB_END\n")
        for frame in frames:
            class Port:
                chunks = iter([frame])

                def read(self, count):
                    return next(self.chunks)

                def write(self, data):
                    return len(data)

            with self.subTest(frame=frame), tempfile.TemporaryDirectory() as tmp:
                output = Path(tmp) / "runtime-fdt.txt"
                with self.assertRaises(ExportError):
                    collector.capture(Port(), output, "dtb", b"DTB")
                self.assertFalse(output.exists())
                self.assertEqual(output.with_suffix(".txt.partial").read_bytes(), frame)

    def test_completed_control_failure_still_collects_kernel_and_fdt(self):
        frame = b"DISPLAY_CONTROL_BEGIN\nDISPLAY_CONTROL value=1500 readback=0 rc=-1 (software readback only)\nDISPLAY_CONTROL_END\n"
        with self.assertRaises(collector.ControlError):
            validate_frame(frame, b"DISPLAY_CONTROL")
        calls = []
        def capture(port, output, command, kind):
            calls.append(command)
            if command == "display-on":
                raise collector.ControlError("backlight control failed")
        with patch.object(collector, "capture", capture):
            self.assertEqual(collector.capture_sequence(None, Path("unused"), True),
                             "backlight control failed")
        self.assertEqual(calls, ["display", "display-on", "display", "dmesg", "dtb"])

    def test_transport_failure_stops_without_more_console_commands(self):
        calls = []
        def capture(port, output, command, kind):
            calls.append(command)
            if command == "display-on":
                raise ExportError("USB disconnected")
        with patch.object(collector, "capture", capture), self.assertRaises(ExportError):
            collector.capture_sequence(None, Path("unused"), True)
        self.assertEqual(calls, ["display", "display-on"])

    def test_display_sequence_can_collect_vblank_measurement(self):
        calls = []
        def capture(port, output, command, kind):
            calls.append((command, kind))
        with patch.object(collector, "capture", capture):
            self.assertIsNone(collector.capture_sequence(None, Path("unused"), False,
                                                         measure_vblank=True))
        self.assertEqual(calls, [("display", b"DISPLAY"),
                                 ("display-vblank", b"DISPLAY_VBLANK"),
                                 ("dmesg", b"KMSG"), ("dtb", b"DTB")])

    def test_vblank_frame_requires_single_result(self):
        frame = (b"DISPLAY_VBLANK_BEGIN\n"
                 b"DISPLAY_VBLANK device=/dev/dri/card1 crtc=0 "
                 b"source=DRM_IOCTL_WAIT_VBLANK\n"
                 b"DISPLAY_VBLANK_RESULT events=91 sequence_gaps=0 span_ms=1000.000 "
                 b"avg_hz=90.000 min_hz=90.000 max_hz=90.000 errno=0 rc=0\n"
                 b"DISPLAY_VBLANK_END\n")
        self.assertEqual(validate_frame(frame, b"DISPLAY_VBLANK"), frame)
        with self.assertRaises(ExportError):
            validate_frame(frame.replace(b"DISPLAY_VBLANK_END", b"DISPLAY_VBLANK_END\n"
                                                                     b"DISPLAY_VBLANK_END"),
                           b"DISPLAY_VBLANK")
        no_result = frame.replace(frame.splitlines()[2] + b"\n", b"")
        with self.assertRaises(ExportError):
            validate_frame(no_result, b"DISPLAY_VBLANK")

    def test_missing_reordered_failed_and_dropped_frames(self):
        frame = b"KMSG_BEGIN\nKMSG_SNAPSHOT bytes=4 ring_bytes=4096 filtered=0 clear=0\nabc\n\nKMSG_NOTE ring\nKMSG_END\n"
        self.assertEqual(validate_frame(frame, b"KMSG"), frame)
        for changed in (frame.replace(b"abc", b"ab"), frame[:-10], frame + frame,
                        b"KMSG_END\nKMSG_BEGIN\n", b"KMSG_BEGIN\nKMSG_ERROR unavailable\nKMSG_END\n"):
            with self.subTest(changed=changed), self.assertRaises(ExportError):
                validate_frame(changed, b"KMSG")


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cc"),
                     "Linux and C compiler required")
class DisplayDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        cls.binary = Path(cls.build.name) / "display"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", str(ROOT / "tests/display_diagnostics_harness.c"),
                        "-o", str(cls.binary)], check=True, capture_output=True)

    def invoke(self, path, mode=0):
        return subprocess.check_output([str(self.binary), str(path), str(mode)], text=True)

    def backlight(self, root, name, compatible=b"xiaomi,umi-csot\0", maximum="2047"):
        node = root / name
        (node / "device/of_node").mkdir(parents=True)
        (node / "device/of_node/compatible").write_bytes(compatible)
        (node / "max_brightness").write_text(maximum)
        (node / "brightness").write_text("1")
        return node

    def test_backlight_rejects_unknown_ambiguous_and_malformed_targets_before_writes(self):
        for compatible, maximum, duplicate in [(b"xiaomi,umi\0", "2047", False),
                (b"xiaomi,umi-csot\0", "2047garbage", False),
                (b"xiaomi,umi-csot\0", "1023", False),
                (b"xiaomi,umi-csot\0", "2047", True)]:
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.backlight(root, "panel", compatible, maximum)
                if duplicate:
                    self.backlight(root, "second")
                self.assertIn("TEST writes=0 rc=-1", self.invoke(root))

    def test_backlight_transition_failures_and_readback(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            node = self.backlight(root, "panel")
            self.assertIn("TEST writes=2 rc=0", self.invoke(root))
            self.assertEqual((node / "brightness").read_text(), "1500")
            self.assertIn("TEST writes=1 rc=-1", self.invoke(root, 3))
            self.assertIn("TEST writes=2 rc=-1", self.invoke(root, 4))
            self.assertIn("TEST writes=0 rc=-1", self.invoke(root, 5))

    def test_kmsg_preserves_unfiltered_messages_and_reports_unavailable(self):
        self.assertIn("unfiltered drm ready", self.invoke("kmsg"))
        for mode in (1, 2):
            self.assertIn("KMSG_ERROR", self.invoke("kmsg", mode))

    def test_vblank_probe_reports_rate_and_sequence_gaps(self):
        output = self.invoke("vblank")
        self.assertIn("events=91 sequence_gaps=0", output)
        self.assertIn("event_avg_hz=90.000", output)
        self.assertNotIn(" avg_hz=", output)
        self.assertIn("not an optical panel-scan measurement", output)
        self.assertIn("rc=0", output)

    def test_vblank_probe_stops_on_ioctl_error(self):
        output = self.invoke("vblank", 6)
        self.assertIn("events=4", output)
        self.assertIn("errno=22 rc=-1", output)

    def test_vblank_probe_counts_missed_sequences(self):
        output = self.invoke("vblank", 9)
        self.assertIn("events=91 sequence_gaps=1", output)
        self.assertIn("rc=0", output)

    def test_vblank_probe_reports_missing_drm_device(self):
        output = self.invoke("vblank", 7)
        self.assertIn("events=0 sequence_gaps=0", output)
        self.assertIn("errno=19 rc=-1", output)
