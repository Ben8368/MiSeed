# SPDX-License-Identifier: AGPL-3.0-only
from contextlib import redirect_stderr
from io import StringIO
import unittest
from unittest.mock import patch

from tools import collect_wireless_rx as collector


def sample(overrides=None):
    values = dict.fromkeys(collector.STATUS_FIELDS + collector.RX_TELEMETRY + (collector.TYPEC_MODE_END,), "")
    values.update(dict.fromkeys(collector.REQUIRED, "0"))
    values["battery/status"] = "Not charging"
    values[collector.TYPEC_MODE] = values[collector.TYPEC_MODE_END] = "Source attached (default current)"
    values.update(overrides or {})
    return "".join(f"{key}\t{value}\r\n" for key, value in values.items()).encode("ascii")


def wired_sample(overrides=None):
    return sample({"usb/present": "1", collector.RX_PIN_ENABLED: "0", **(overrides or {})})


class WirelessRxCaptureTest(unittest.TestCase):
    def capture(self, kernel=collector.KERNEL, binding=b"5-003b\t\n5-0061\trx1619\n",
                boot_after=b"11111111-1111-1111-1111-111111111111", uid_after=b"2000",
                stage=collector.USB_BASELINE, samples=None, boot_before=None):
        boot = b"11111111-1111-1111-1111-111111111111"
        reads = [boot, b"umi", kernel, b"23.2-20261115-NIGHTLY-umi", b"2000",
                 boot_before or boot, binding,
                 *(samples or [sample()] * 3), boot_after, uid_after]
        with patch.object(collector, "adb_read", side_effect=reads) as adb_read, patch.object(
                collector.time, "sleep"):
            report = collector.collect("unused", "private-device-serial", stage, False)
        self.assertTrue(all(call.args[2] in ("shell", "exec-out") for call in adb_read.call_args_list))
        return report

    def test_snapshots_keep_zero_and_distinguish_unavailable_values(self):
        result = self.capture()
        self.assertNotIn("private-device-serial", repr(result))
        self.assertEqual(result["bindings"]["5-0061"], "rx1619")
        self.assertIsNone(result["bindings"]["5-003b"])
        self.assertEqual(result["uid_after"], 2000)
        self.assertEqual(len(result["samples"]), 3)
        fields = result["samples"][0]["fields"]
        self.assertEqual(fields[collector.POWER_GOOD], "0")
        self.assertIsNone(fields["wireless/rx_iout"])
        self.assertEqual(fields["usb/typec_mode"], "Source attached (default current)")

    def test_wrong_kernel_binding_boot_or_privilege_rejected(self):
        for args in ({"kernel": b"other"}, {"binding": b"5-003b\tidt\n5-0061\trx1619\n"},
                     {"boot_before": b"22222222-2222-2222-2222-222222222222"},
                     {"boot_after": b"22222222-2222-2222-2222-222222222222"}, {"uid_after": b"0"}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.capture(**args)

    def test_required_missing_invalid_duplicate_and_partial_frames_rejected(self):
        for data in (sample({collector.POWER_GOOD: ""}), sample({"rx1619/present": "2"}),
                     sample() + b"usb/present\t1\n", b"usb/present\t0\n"):
            with self.subTest(data=data), patch.object(collector, "adb_read", return_value=data):
                with self.assertRaises(ValueError):
                    collector.snapshot("unused", "private-device-serial")

    def test_wired_capture_retains_contradictory_rx_readbacks_without_passing_policy(self):
        data = wired_sample({collector.RX_PIN_ENABLED: "1", collector.POWER_GOOD: "1",
                             "wireless/rx_iout": "87"})
        for stage in collector.WIRED_STAGES:
            with self.subTest(stage=stage):
                result = self.capture(stage=stage, samples=[wired_sample(), data, wired_sample()])
                self.assertEqual(result["stage"], stage)
                self.assertEqual([s["fields"][collector.RX_PIN_ENABLED] for s in result["samples"]],
                                 ["0", "1", "0"])
                self.assertEqual(result["samples"][1]["fields"]["wireless/rx_iout"], "87")
                self.assertNotIn("passed", result)

    def test_wired_capture_rejects_shared_usbin_alone_missing_pin_or_typec_changes(self):
        source_high = "Source attached (high current)"
        for bad in (wired_sample({collector.TYPEC_MODE: "Nothing attached",
                                  collector.TYPEC_MODE_END: "Nothing attached"}),
                    wired_sample({collector.TYPEC_MODE_END: "Nothing attached"}),
                    wired_sample({collector.TYPEC_MODE: "Sink attached",
                                  collector.TYPEC_MODE_END: "Sink attached"}),
                    wired_sample({"usb/present": "0"}), wired_sample({collector.RX_PIN_ENABLED: ""}),
                    wired_sample({collector.RX_PIN_ENABLED: "2"}),
                    wired_sample({collector.TYPEC_MODE: source_high, collector.TYPEC_MODE_END: source_high})):
            with self.subTest(sample=bad), self.assertRaises(ValueError):
                self.capture(stage="wired-on-pad", samples=[wired_sample(), bad, wired_sample()])

    def test_wired_stage_requires_paired_wifi_before_connecting(self):
        with (redirect_stderr(StringIO()) as stderr,
              patch.object(collector, "output_path"), patch.object(collector.subprocess, "check_output") as adb,
              patch.object(collector, "collect") as collect, self.assertRaises(SystemExit) as failure):
            collector.main(["--adb", "unused", "--stage", "wired-on-pad", "--output", "unused.json"])
        self.assertEqual(failure.exception.code, 1)
        self.assertIn("paired Wi-Fi", stderr.getvalue())
        adb.assert_not_called()
        collect.assert_not_called()

    def test_cli_rejects_temporary_root_over_wifi_before_connecting(self):
        with (redirect_stderr(StringIO()) as stderr,
              patch.object(collector, "collect") as collect,
              self.assertRaises(SystemExit) as failure):
            collector.main(["--adb", "unused", "--stage", "on-pad", "--output", "unused.json",
                            "--wifi-serial", "192.0.2.1:12345", "--temporary-adb-root"])
        self.assertEqual(failure.exception.code, 2)
        self.assertIn("switch adbd privilege over USB", stderr.getvalue())
        collect.assert_not_called()
