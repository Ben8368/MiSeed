"""Linux synthetic USB diagnostics; never run PID 1 or touch a USB gadget."""

# SPDX-License-Identifier: AGPL-3.0-only

import errno
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
from rescue_test_support import RescueFixture


class RescueConsoleTests(RescueFixture):
    def test_unbound_pon_refuses_without_reboot_syscall(self):
        result = subprocess.run([self.program, "bootloader-unbound"],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Bootloader request refused", result.stderr)


    def test_late_udc_and_failed_bind_are_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "udc").mkdir()
            env = dict(os.environ, TEST_UDC_DIR=str(root / "udc"), TEST_UDC_BIND=str(root / "bound"))
            result = subprocess.run([self.program, "bind"], env=env, capture_output=True,
                                    text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "bound").read_text(), "a600000.usb")
            self.assertIn("gadget bound", result.stdout)


    def test_commands_work_after_two_terminal_hangups(self):
        import pty

        with tempfile.TemporaryDirectory() as directory:
            link = Path(directory) / "tty"
            master, slave = pty.openpty()
            link.symlink_to(os.ttyname(slave))
            env = dict(os.environ, TEST_TTY=str(link))
            process = subprocess.Popen([self.program, "console"], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            os.close(slave)
            try:
                for connection in range(3):
                    self.read_until(master, b"miseed> ")
                    os.write(master, b"unknown-command\n")
                    self.read_until(master, b"Unknown command")
                    if connection != 2:
                        os.close(master)
                        master, slave = pty.openpty()
                        link.unlink()
                        link.symlink_to(os.ttyname(slave))
                        os.close(slave)
                self.assertIsNone(process.poll())
            finally:
                process.terminate()
                process.wait(timeout=5)
                os.close(master)


    def test_charge_commands_select_explicit_duration_and_stop(self):
        import pty

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            supply = root / "pm8150b-charger"
            supply.mkdir()
            control = supply / "charge_test"
            control.touch()
            behaviour = supply / "charge_behaviour"
            behaviour.touch()
            master, slave = pty.openpty()
            env = dict(os.environ, TEST_TTY=os.ttyname(slave), TEST_POWER_ROOT=str(root))
            process = subprocess.Popen([self.program, "console"], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            os.close(slave)
            try:
                banner = self.read_until(master, b"miseed> ")
                self.assertIn(b"read-only samples", banner)
                self.assertIn(b"require a diagnostic kernel", banner)
                for command, value, message in (
                    (b"charge-test\n", "arm", b"after 60 seconds"),
                    (b"charge-test-10min\n", "arm-10min", b"after 600 seconds"),
                    (b"charge-stop\n", "inhibit-charge", b"test cancelled"),
                ):
                    control.write_text("")
                    os.write(master, command)
                    self.read_until(master, message)
                    self.assertEqual((behaviour if command == b"charge-stop\n" else control).read_text(), value)
            finally:
                process.terminate()
                process.wait(timeout=5)
                os.close(master)


    def test_pps_discovery_refuses_missing_or_ambiguous_devices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            env = dict(os.environ, TEST_PPS_ROOT=str(root))
            self.assertIn("discovery=-1", self.run_log("pps-discovery", env))
            first = root / "15-0066"
            first.mkdir()
            control = first / "charge_behaviour"
            (first / "model_name").write_text("BQ25970\n")
            (first / "uevent").write_text("POWER_SUPPLY_STATUS=Charging\n")
            control.touch()
            self.assertIn("discovery=0", self.run_log("pps-discovery", env))
            self.assertEqual(control.read_text(), "")
            second = root / "16-0066"
            second.mkdir()
            (second / "charge_behaviour").touch()
            (second / "model_name").write_text("BQ25970\n")
            control.write_text("keep")
            self.assertIn("discovery=-1", self.run_log("pps-discovery", env))
            self.assertEqual(control.read_text(), "keep")


    def test_pps_console_automatic_status_and_unified_stop(self):
        import pty

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pump = root / "pump0"
            pump.mkdir(parents=True)
            control = pump / "charge_behaviour"
            (pump / "model_name").write_text("BQ25970\n")
            (pump / "uevent").write_text("POWER_SUPPLY_STATUS=Charging\n")
            control.touch()
            supply = root / "pm8150b-charger"
            supply.mkdir()
            normal = supply / "charge_behaviour"
            normal.touch()
            master, slave = pty.openpty()
            env = dict(os.environ, TEST_TTY=os.ttyname(slave), TEST_POWER_ROOT=str(root),
                       TEST_PPS_ROOT=str(root))
            process = subprocess.Popen([self.program, "console"], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            os.close(slave)
            try:
                self.read_until(master, b"miseed> ")
                for command, value, message in (
                    (b"pps-status\n", "auto\n", b"POWER_SUPPLY_STATUS=Charging"),
                    (b"pps-stop\n", "inhibit-charge", b"PPS stopped; SMB5 fallback policy resumed."),
                    (b"charge-stop\n", "inhibit-charge", b"test cancelled"),
                ):
                    control.write_text(value if command == b"pps-status\n" else "")
                    os.write(master, command)
                    self.read_until(master, message)
                    self.assertEqual(control.read_text(), value)
                self.assertEqual(normal.read_text(), "inhibit-charge")
            finally:
                process.terminate()
                process.wait(timeout=5)
                os.close(master)


    def test_pps_status_read_error_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "15-0066").mkdir()
            (root / "15-0066/charge_behaviour").write_text("auto\n")
            (root / "15-0066/model_name").write_text("BQ25970\n")
            env = dict(os.environ, TEST_PPS_ROOT=str(root), TEST_PPS_READ_FAIL="1")
            result = subprocess.run([self.program, "pps-discovery"], env=env, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 23)


if __name__ == "__main__":
    unittest.main()
