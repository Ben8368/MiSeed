"""Reported measurement, capture failures and board-identity boundary."""
# SPDX-License-Identifier: AGPL-3.0-only
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import collect_display_evidence as display, collect_nfc_evidence as nfc
from tools.report_dpu_fps import report_fps


class EvidenceTests(unittest.TestCase):
    def test_dpu_counts_only_selected_engine_inside_window(self):
        sample = """# entries-in-buffer/entries-written: 8/8   #P:8
a [001] ... 10.00001: dpu_enc_phys_cmd_pp_tx_done: id=35, pp=0, new_count=0, event=1
a [001] ... 10.10000: tracing_mark_write: miseed_fps_begin
a [001] ... 10.20000: dpu_enc_phys_cmd_pp_tx_done: id=35, pp=0, new_count=0, event=1
a [001] ... 10.20001: dpu_enc_phys_cmd_pp_tx_done: id=36, pp=0, new_count=0, event=1
a [001] ... 10.40000: dpu_enc_phys_cmd_pp_tx_done: id=35, pp=1, new_count=0, event=1
a [001] ... 11.10000: dpu_enc_phys_cmd_pp_tx_done: id=35, pp=0, new_count=1, event=1
a [001] ... 12.10000: tracing_mark_write: miseed_fps_end
a [001] ... 12.21000: dpu_enc_phys_cmd_pp_tx_done: id=35, pp=0, new_count=0, event=1
"""
        result = report_fps(sample, 35, 0)
        self.assertEqual((result["dpu_tx_fps"], result["tx_completed"]), (1, 2))
        for broken in (sample.replace("8/8", "8/9"), sample.replace("_end", "_begin"),
                       sample.replace("12.10000", "10.10001")):
            with self.assertRaises(ValueError):
                report_fps(broken, 35, 0)

    def test_failed_vblank_and_partial_capture_are_not_display_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = display.display_report(out, b"", True, False, "USB timeout")
            self.assertEqual(result["checks"]["display_vblank"], "INCOMPLETE")
            self.assertEqual(result["result"], "FAIL")
            (out / "display-vblank.txt").write_bytes(
                b"DISPLAY_VBLANK_BEGIN\nDISPLAY_VBLANK_RESULT events=4 sequence_gaps=1 "
                b"event_avg_hz=59.9 rc=-1\nDISPLAY_VBLANK_END\n")
            result = display.display_report(out, b"", True, False, "DTB timeout")
            self.assertEqual(result["checks"]["display_vblank"], "FAIL")
            self.assertEqual(result["checks"]["physical_panel_hz"], "NOT_MEASURED")

    def test_nfc_failed_complete_frame_keeps_logs_but_does_not_retry(self):
        calls = []

        def capture(port, out, command, kind, **kwargs):
            calls.append(command)
            if command == "nfc-status":
                out.write_bytes(b"NFC I2C device=1-0028 driver=sn100x\n"
                                b"NFC DT matches=1 scan=finished\nNFC class devices=1 (all)\n")
            if command == "nfc-l3":
                self.assertEqual(kwargs["timeout"], 120)
                out.write_bytes(b"NFC_L3_BEGIN\nNFC_L3_RESULT rc=-6\nNFC_L3_END\n")

        with tempfile.TemporaryDirectory() as tmp, patch.object(nfc, "capture", capture):
            report = {"issues": []}
            nfc.collect(None, Path(tmp), report)
        self.assertEqual(calls, ["nfc-status", "nfc-l3", "dmesg"])
        self.assertEqual(report["probe_rc"], -6)
        self.assertEqual(report["issues"][0]["section"], "nfc_l3")

    def test_nfc_rejects_unsubstantiated_success_and_ambiguous_identity(self):
        frame = b"NFC_L3_BEGIN\nNFC_L3_RESULT rc=0\nNFC_L3_END\n"
        with self.assertRaises(display.ExportError):
            display.validate_frame(frame, b"NFC_L3")
        with self.assertRaises(display.ExportError):
            nfc.check_identity(b"NFC I2C device=1-0028 driver=sn100x\n"
                               b"NFC DT matches=1 scan=finished\nNFC class devices=2 (all)\n")
