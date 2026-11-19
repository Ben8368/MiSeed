# SPDX-License-Identifier: AGPL-3.0-only
from contextlib import redirect_stderr
from io import StringIO
import unittest
from unittest.mock import patch

from tools import collect_wireless_rx as collector


def sample(overrides=None):
    values = dict.fromkeys(collector.STATUS_FIELDS + collector.RX_TELEMETRY, "")
    values.update(dict.fromkeys(collector.REQUIRED, "0"))
    values["battery/status"] = "Not charging"
    values["usb/typec_mode"] = "Source attached (default current)"
    values.update(overrides or {})
    return "".join(f"{key}\t{value}\r\n" for key, value in values.items()).encode("ascii")


class WirelessRxCaptureTest(unittest.TestCase):
    def capture(self, kernel=collector.KERNEL, binding=b"5-003b\t\n5-0061\trx1619\n",
                boot_after=b"11111111-1111-1111-1111-111111111111", uid_after=b"2000"):
        reads = [b"umi", kernel, b"23.2-20261115-NIGHTLY-umi", b"2000",
                 b"11111111-1111-1111-1111-111111111111", binding,
                 sample(), sample(), sample(), boot_after, uid_after]
        with patch.object(collector, "adb_read", side_effect=reads) as adb_read, patch.object(
                collector.time, "sleep"):
            report = collector.collect("unused", "private-device-serial", collector.USB_BASELINE, False)
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
                     {"boot_after": b"22222222-2222-2222-2222-222222222222"}, {"uid_after": b"0"}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.capture(**args)

    def test_required_missing_invalid_duplicate_and_partial_frames_rejected(self):
        for data in (sample({collector.POWER_GOOD: ""}), sample({"rx1619/present": "2"}),
                     sample() + b"usb/present\t1\n", b"usb/present\t0\n"):
            with self.subTest(data=data), patch.object(collector, "adb_read", return_value=data):
                with self.assertRaises(ValueError):
                    collector.snapshot("unused", "private-device-serial")

    def test_cli_rejects_temporary_root_over_wifi_before_connecting(self):
        with (redirect_stderr(StringIO()) as stderr,
              patch.object(collector, "collect") as collect,
              self.assertRaises(SystemExit) as failure):
            collector.main(["--adb", "unused", "--stage", "on-pad", "--output", "unused.json",
                            "--wifi-serial", "192.0.2.1:12345", "--temporary-adb-root"])
        self.assertEqual(failure.exception.code, 2)
        self.assertIn("switch adbd privilege over USB", stderr.getvalue())
        collect.assert_not_called()
