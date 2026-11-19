"""Offline Fastboot failure classification and identifier redaction regressions."""
# SPDX-License-Identifier: AGPL-3.0-only

import json
import plistlib
import subprocess
import unittest

from tools.check_fastboot_connection import inspect_connection


SECRET = "private-device-selector"


def target(interfaces=()):
    return [{"idVendor": 0x18d1, "idProduct": 0xd00d,
             "USB Serial Number": SECRET,
             "IORegistryEntryChildren": list(interfaces)}]


class FastbootConnectionTests(unittest.TestCase):
    def probe(self, stdout=b"", tree=None, fastboot_exit=0, usb_exit=0, failure=None):
        calls = []

        def runner(command, **kwargs):
            calls.append(command)
            if len(calls) == 1:
                if failure:
                    raise failure
                return subprocess.CompletedProcess(command, fastboot_exit, stdout, SECRET.encode())
            data = plistlib.dumps(tree if tree is not None else [])
            return subprocess.CompletedProcess(command, usb_exit, data, SECRET.encode())

        report, selector = inspect_connection("fastboot", runner=runner, platform="darwin")
        self.assertNotIn(SECRET, json.dumps(report))
        for command in calls:
            self.assertNotIn("boot", command)
            self.assertNotIn("flash", command)
        return report, selector, calls

    def test_ready_keeps_selector_out_of_report_and_skips_usb_slow_path(self):
        report, selector, calls = self.probe((SECRET + "\tfastboot\n").encode())
        self.assertEqual(report["state"], "ready_for_identity")
        self.assertEqual(selector, SECRET)
        self.assertEqual(len(calls), 1)

    def test_two_devices_stop_without_usb_or_identity_commands(self):
        report, selector, calls = self.probe(b"one fastboot\ntwo fastboot\n")
        self.assertEqual(report["state"], "multiple_devices")
        self.assertIsNone(selector)
        self.assertEqual(len(calls), 1)

    def test_malformed_enumeration_fails_closed(self):
        report, selector, _ = self.probe((SECRET + " recovery\n").encode())
        self.assertEqual(report["state"], "invalid_enumeration")
        self.assertIsNone(selector)

    def test_usb_target_without_interfaces_does_not_mean_missing_cable(self):
        report, _, calls = self.probe(tree=target())
        self.assertEqual(report["state"], "usb_interfaces_unavailable")
        self.assertEqual(len(calls), 2)
        self.assertIn("-l", calls[1])

    def test_published_fastboot_interface_has_distinct_failure_state(self):
        interface = {"bInterfaceClass": 255, "bInterfaceSubClass": 66, "bInterfaceProtocol": 3}
        report, _, _ = self.probe(tree=target([interface]))
        self.assertEqual(report["state"], "fastboot_access_failed")

    def test_other_interface_is_not_fastboot(self):
        report, _, _ = self.probe(tree=target([{"bInterfaceClass": 2}]))
        self.assertEqual(report["state"], "usb_interfaces_unavailable")

    def test_no_usb_target(self):
        report, _, _ = self.probe()
        self.assertEqual(report["state"], "not_detected")

    def test_usb_permission_failure_is_not_no_device(self):
        report, _, _ = self.probe(usb_exit=1)
        self.assertEqual(report["state"], "usb_visibility_unverified")

    def test_fastboot_nonzero_is_not_no_device(self):
        report, _, calls = self.probe(fastboot_exit=1)
        self.assertEqual(report["state"], "fastboot_tool_failed")
        self.assertEqual(len(calls), 1)

    def test_timeout_is_bounded_and_does_not_retry(self):
        report, _, calls = self.probe(failure=subprocess.TimeoutExpired("fastboot", 2))
        self.assertEqual(report["fastboot_error"], "timeout")
        self.assertEqual(len(calls), 1)

    def test_missing_binary_is_not_no_device(self):
        report, _, _ = self.probe(failure=FileNotFoundError())
        self.assertEqual(report["fastboot_error"], "unavailable")

    def test_deadline_validation_prevents_unbounded_wait(self):
        for timeout in (0, -1, 6, float("nan")):
            with self.assertRaises(ValueError):
                inspect_connection(timeout=timeout)
