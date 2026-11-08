"""Offline adb fixtures: no attached phone or adb process is used."""

# SPDX-License-Identifier: AGPL-3.0-only

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import collect_device


SERIAL = "private-device-selector"


def success(argv, **kwargs):
    command = tuple(argv[3:])
    if command == ("get-state",):
        value = "device\n"
    elif command == ("shell", "getprop", "ro.product.device"):
        value = "umi\n"
    elif command == collect_device.PARTITIONS:
        value = "lrwxrwxrwx root root boot_a -> /dev/block/sda1\n"
    else:
        value = "fixture-value\n"
    return subprocess.CompletedProcess(argv, 0, value, "")


class DeviceCollection(unittest.TestCase):
    def test_success_commands_and_evidence_boundary(self):
        with patch.object(collect_device.subprocess, "run", side_effect=success) as run:
            report = collect_device.collect_identity("adb", SERIAL)
        self.assertEqual(report["collection_status"], "complete")
        self.assertEqual(report["validation"], "device_reported_only")
        self.assertIn("backup_and_rollback_plan", report["unresolved"])
        self.assertNotIn(SERIAL, json.dumps(report))
        self.assertEqual(len(run.call_args_list), len(collect_device.QUERIES))
        for call in run.call_args_list:
            self.assertEqual(call.args[0][:3], ["adb", "-s", SERIAL])
            self.assertIn(tuple(call.args[0][3:]), collect_device.QUERIES)
            self.assertFalse(call.kwargs["shell"])
            self.assertEqual(call.kwargs["timeout"], 10)

    def test_power_collects_actual_id_and_cached_pdos_without_raw_logs(self):
        def fixture(argv, **kwargs):
            if argv[-1] == "dmesg":
                return subprocess.CompletedProcess(argv, 0,
                    "private unrelated " + SERIAL + "\n"
                    "[bq2597x] bq2597x_detect_device: detect device:16\n", "")
            return success(argv, **kwargs)
        with patch.object(collect_device.subprocess, "run", side_effect=fixture) as run:
            report = collect_device.collect_identity("adb", SERIAL, power=True)
        self.assertEqual(report["observations"]["pump_ids"]["value"], [16])
        self.assertNotIn(SERIAL, json.dumps(report))
        self.assertEqual(report["collection_status"], "complete")
        for call in run.call_args_list:
            self.assertIn(tuple(call.args[0][3:]), collect_device.QUERIES + collect_device.POWER_QUERIES)

    def test_pump_model_name_or_ambiguous_log_is_not_a_chip_id(self):
        for text in ("bq2597x-standalone", "[bq2597x] bq2597x_detect_device: detect device:16\n"
                     "[bq2597x] bq2597x_detect_device: detect device:65"):
            def fixture(argv, **kwargs):
                if argv[-1] == "dmesg":
                    return subprocess.CompletedProcess(argv, 0, text, "")
                return success(argv, **kwargs)
            with patch.object(collect_device.subprocess, "run", side_effect=fixture):
                report = collect_device.collect_identity("adb", SERIAL, power=True)
            self.assertEqual(report["observations"]["pump_ids"]["status"], "unavailable")
            self.assertEqual(report["collection_status"], "partial")

    def test_changing_pd_capabilities_are_not_a_qualified_snapshot(self):
        counter = 0
        def fixture(argv, **kwargs):
            nonlocal counter
            if argv[-1].endswith("/src_cap_id"):
                counter += 1
                return subprocess.CompletedProcess(argv, 0, str(counter), "")
            return success(argv, **kwargs)
        with patch.object(collect_device.subprocess, "run", side_effect=fixture):
            report = collect_device.collect_identity("adb", SERIAL, power=True)
        self.assertEqual(report["observations"]["source_pdos"]["status"], "unavailable")
        with patch.object(collect_device.subprocess, "run") as run:
            for path in ("/sys/class/usbpd/usbpd0/get_src_cap_ext", "/sys/bus/i2c/devices/0-0066/registers"):
                with self.assertRaises(collect_device.CollectionError):
                    collect_device.query("adb", SERIAL, ("shell", "cat", path))
            run.assert_not_called()

    def test_disconnected_unauthorized_and_wrong_device_stop(self):
        for value, code in [("unauthorized", 0), ("offline", 0), ("", 1)]:
            with self.subTest(state=value), patch.object(
                collect_device.subprocess, "run",
                return_value=subprocess.CompletedProcess([], code, value, SERIAL),
            ) as run:
                with self.assertRaises(collect_device.CollectionError):
                    collect_device.collect_identity("adb", SERIAL)
                self.assertEqual(run.call_count, 1)
        def wrong_device(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 0, "device" if argv[-1] == "get-state"
                                               else "other-model", "")
        with patch.object(collect_device.subprocess, "run", side_effect=wrong_device) as run:
            with self.assertRaises(collect_device.CollectionError):
                collect_device.collect_identity("adb", SERIAL)
            self.assertEqual(run.call_count, 2)

    def test_partial_report_preserves_failures_without_raw_errors(self):
        def partial(argv, **kwargs):
            if argv[-1] == "/dev/block/by-name":
                return subprocess.CompletedProcess(argv, 1, "", "denied " + SERIAL)
            if argv[-1] == "ro.boot.bootloader":
                return subprocess.CompletedProcess(argv, 0, "\n", "")
            return success(argv, **kwargs)
        with patch.object(collect_device.subprocess, "run", side_effect=partial):
            report = collect_device.collect_identity("adb", SERIAL)
        self.assertEqual(report["collection_status"], "partial")
        self.assertEqual(report["observations"]["partition_links"]["error"], "adb_exit")
        self.assertEqual(report["observations"]["ro.boot.bootloader"]["status"], "unavailable")
        self.assertNotIn(SERIAL, json.dumps(report))

    def test_timeout_and_missing_executable_are_sanitized(self):
        for error, label in [(subprocess.TimeoutExpired(["adb", SERIAL], 10), "timeout"),
                             (FileNotFoundError(SERIAL), "adb_unavailable")]:
            with self.subTest(label=label), patch.object(
                collect_device.subprocess, "run", side_effect=error,
            ):
                result = collect_device.query("adb", SERIAL, ("get-state",))
                self.assertEqual(result, {"status": "error", "value": None, "error": label})

    def test_forbidden_command_and_invalid_selector_never_execute(self):
        with patch.object(collect_device.subprocess, "run") as run:
            for command in [("root",), ("reboot",), ("shell", "getprop"),
                            ("shell", "dd", "of=/dev/block/sda")]:
                with self.subTest(command=command), self.assertRaises(collect_device.CollectionError):
                    collect_device.query("adb", SERIAL, command)
            for serial in ["", "-d", "two devices", "bad\nselector", "bad\x00selector"]:
                with self.subTest(serial=serial), self.assertRaises(collect_device.CollectionError):
                    collect_device.collect_identity("adb", serial)
            run.assert_not_called()

    def test_output_containment_and_existing_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            self.assertEqual(collect_device.report_path(root, Path("local/device.json")),
                             root / "local/device.json")
            for output in ["device.json", "local/../../escape.json", "local/device.img"]:
                with self.subTest(output=output), self.assertRaises(collect_device.CollectionError):
                    collect_device.report_path(root, Path(output))
            (root / "local").mkdir()
            existing = root / "local/device.json"
            existing.write_text("keep\n", encoding="utf-8")
            with self.assertRaises(collect_device.CollectionError):
                collect_device.report_path(root, existing)
            self.assertEqual(existing.read_text(encoding="utf-8"), "keep\n")

    def test_cli_success_partial_and_error_exit_codes(self):
        for partial in [False, True]:
            with self.subTest(partial=partial), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                def fixture(argv, **kwargs):
                    if partial and argv[-1] == "/dev/block/by-name":
                        return subprocess.CompletedProcess(argv, 1, "", "denied")
                    return success(argv, **kwargs)
                with patch.object(collect_device, "ROOT", root), patch.object(
                    collect_device.shutil, "which", return_value="adb",
                ), patch.object(collect_device.subprocess, "run", side_effect=fixture), patch("builtins.print"):
                    code = collect_device.main(["--serial", SERIAL])
                self.assertEqual(code, 1 if partial else 0)
                output = root / "local/device-identity.json"
                data = output.read_bytes()
                self.assertNotIn(b"\r", data)
                self.assertNotIn(SERIAL, data.decode("utf-8"))
                self.assertEqual(json.loads(data)["scope"], "android_read_only_metadata")
                with patch.object(collect_device, "ROOT", root), patch.object(
                    collect_device.subprocess, "run",
                ) as run, patch("builtins.print"):
                    self.assertEqual(collect_device.main(["--serial", SERIAL]), 2)
                    run.assert_not_called()
                self.assertEqual(output.read_bytes(), data)

    def test_cli_missing_adb_and_identity_failure_do_not_write_report(self):
        for adb in [None, "adb"]:
            with self.subTest(adb=adb), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                with patch.object(collect_device, "ROOT", root), patch.object(
                    collect_device.shutil, "which", return_value=adb,
                ), patch.object(collect_device.subprocess, "run", side_effect=FileNotFoundError), patch("builtins.print"):
                    self.assertEqual(collect_device.main(["--serial", SERIAL]), 2)
                self.assertFalse((root / "local").exists())


if __name__ == "__main__":
    unittest.main()
