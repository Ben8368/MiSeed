"""Reported measurement, capture failures and board-identity boundary."""
# SPDX-License-Identifier: AGPL-3.0-only
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import collect_display_evidence as display, collect_nfc_evidence as nfc
from tools.report_dpu_fps import report_fps


STATUS = (b'NFC_STATUS_BEGIN\nNFC I2C device=1-0028 driver=sn100x\nNFC 1-0028 reg=0x28\n'
          b'NFC DT matches=1 scan=finished\nNFC class device=nfc0 (registration metadata only)\n'
          b'NFC class devices=1 (all controllers, not a transport test)\nNFC_STATUS_END\n')


class EvidenceTests(unittest.TestCase):
    def test_first_address_needs_waveform_and_never_runs_rf(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(nfc, 'capture') as capture:
            out = Path(tmp)
            for rf, armed in ((False, False), (True, True)):
                with self.assertRaises(display.ExportError):
                    nfc.collect(None, out, {}, run_rf=rf, first=True, waveform_armed=armed)
            capture.assert_not_called()
            (out/'nfc-status.txt').write_bytes(STATUS)
            for rc in (0, -110):
                capture.reset_mock()
                (out/'nfc-first.txt').write_text(f'NFC_FIRST_BEGIN\nNFC_FIRST_RESULT stage=dev_up rc={rc}\nNFC_FIRST_END\n')
                report = {'issues': []}
                nfc.collect(None, out, report, run_rf=False, first=True, waveform_armed=True)
                self.assertEqual([c.args[2] for c in capture.call_args_list], ['nfc-status','nfc-first','dmesg'])
                self.assertEqual(report['probe_rc'], rc)
                self.assertEqual(bool(report['issues']), bool(rc))
                self.assertEqual(report['ninth_clock'], 'NOT_MEASURED_BY_HOST')

    def test_init_retry_uses_single_dev_up(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(nfc, 'capture') as capture:
            out = Path(tmp)
            for extra in ({'run_rf': True}, {'first': True}, {'waveform_armed': True}, {'read_fixture': True}):
                with self.assertRaises(display.ExportError):
                    nfc.collect(None, out, {}, **({'run_rf': False, 'init_retry': True} | extra))
            capture.assert_not_called()
            (out/'nfc-status.txt').write_bytes(STATUS)
            (out/'nfc-first.txt').write_bytes(b'NFC_FIRST_BEGIN\nNFC_FIRST_RESULT stage=dev_up rc=0\nNFC_FIRST_END\n')
            report = {'issues': []}
            nfc.collect(None, out, report, run_rf=False, init_retry=True)
            self.assertEqual([c.args[2] for c in capture.call_args_list], ['nfc-status', 'nfc-first', 'dmesg'])
            self.assertEqual(report['probe_rc'], 0)

    def test_fixture_read_before_close_failure_remains_evidence(self):
        lines = [b'NFC DEV_UP/reset-init passed.',
                 b'NFC empty-protocol request rejected: Invalid argument (-22).',
                 b'NFC RF discovery passed: targets=1 protocol=16; UID omitted.',
                 b'NFC NDEF Type 4 fixture verified; no UID or payload logged.',
                 b'NFC final DEV_DOWN failed: Connection timed out (-110); inspect dmesg before retry.']
        def frame(body):
            return b'\n'.join([b'NFC_L3_BEGIN', *body, b'NFC_L3_RESULT rc=-110', b'NFC_L3_END'])
        checks = nfc.probe_steps(frame(lines), True)
        self.assertEqual(checks, dict(reset_init='PASS', empty_protocol='PASS', rf_discovery='PASS',
                                      ndef_fixture='PASS', close_reopen='FAIL'))
        for broken in ([*lines[:4], *lines[3:]], [lines[1], lines[0], *lines[2:]],
                       [lines[0], lines[1].replace(b'-22', b'-110'), *lines[2:]]):
            with self.assertRaises(display.ExportError):
                nfc.probe_steps(frame(broken), True)

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
                out.write_bytes(STATUS)
            if command == "nfc-l3":
                self.assertEqual(kwargs["timeout"], 120)
                out.write_bytes(b"NFC_L3_BEGIN\nNFC_L3_RESULT rc=-6\nNFC_L3_END\n")

        with tempfile.TemporaryDirectory() as tmp, patch.object(nfc, "capture", capture):
            report = {"issues": []}
            nfc.collect(None, Path(tmp), report, run_rf=False)
            self.assertEqual(calls, ["nfc-status", "dmesg"])
            calls.clear()
            nfc.collect(None, Path(tmp), report, run_rf=True)
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

    def test_conflicting_or_missing_inventory_stops_before_active_command(self):
        bad_frames = [STATUS.replace(b'NFC_STATUS_END', line + b'\nNFC_STATUS_END') for line in (
            b'NFC DT matches=2 scan=incomplete', b'NFC class devices=0 (all controllers, not a transport test)',
            b'NFC I2C device=3-0028 driver=unbound-or-unavailable', b'NFC class scan truncated after 256 entries')]
        bad_frames.extend((STATUS.replace(b'reg=0x28', b'reg=0x29'),
                           STATUS.replace(b'NFC class device=nfc0 (registration metadata only)\n', b'')))
        for frame in bad_frames:
            with self.subTest(frame=frame), tempfile.TemporaryDirectory() as tmp, patch.object(nfc, 'capture') as capture:
                out = Path(tmp)
                (out / 'nfc-status.txt').write_bytes(frame)
                with self.assertRaises(display.ExportError):
                    nfc.collect(None, out, {}, run_rf=True)
                self.assertEqual([call.args[2] for call in capture.call_args_list], ['nfc-status'])
        nfc.check_identity(STATUS.replace(b'1-0028', b'12-0028').replace(b'nfc0', b'nfc3'))

    def test_l3_result_must_be_unique_and_immediately_before_end(self):
        prefix = (b'NFC_L3_BEGIN\nNFC DEV_UP/reset-init passed.\n'
                  b'NFC RF discovery passed: targets=1 protocol=4; UID omitted.\n'
                  b'NFC close/CORE_RESET/reopen/final close passed; shared VEN was retained.\n')
        valid = prefix + b'NFC_L3_RESULT rc=0\nNFC_L3_END\n'
        self.assertEqual(display.validate_frame(valid, b'NFC_L3'), valid)
        for records in (b'NFC_L3_RESULT rc=0\nNFC_L3_RESULT rc=broken',
                        b'NFC_L3_RESULT rc=broken\nNFC_L3_RESULT rc=-6',
                        b'NFC_L3_RESULT rc=-6\nNFC DEV_UP/reset-init passed.', b'NFC_L3_RESULT rc=-0'):
            frame = prefix + records + b'\nNFC_L3_END\n'
            with self.subTest(records=records), self.assertRaises(display.ExportError):
                display.validate_frame(frame, b'NFC_L3')
