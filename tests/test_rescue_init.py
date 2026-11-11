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


class RescueInitTests(RescueFixture):
    def test_wired_thermal_sysfs_and_bounded_cooling_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cooler = root / "cooling_device7"
            cooler.mkdir()
            (cooler / "type").write_text("umi-wired-pd\n")
            (cooler / "cur_state").write_text("0\n")
            (cooler / "max_state").write_text("1\n")
            zone = root / "thermal_zone8"
            zone.mkdir()
            (zone / "type").write_text("wired-pd-thermal\n")
            (zone / "temp").write_text("29000\n")
            env = dict(os.environ, TEST_THERMAL_ROOT=str(root))

            report = subprocess.run([self.program, "thermal-report"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("type=wired-pd-thermal temp_millic=29000", report.stdout)
            self.assertIn("type=umi-wired-pd cur_state=0 max_state=1", report.stdout)

            stop = subprocess.run([self.program, "thermal-stop"], env=env,
                                  capture_output=True, text=True, timeout=5)
            self.assertEqual(stop.returncode, 0, stop.stderr)
            self.assertIn("old_state=0 requested_state=1 readback=1 max_state=1 rc=0",
                          stop.stdout)
            self.assertEqual((cooler / "cur_state").read_text(), "1\n")

            clear = subprocess.run([self.program, "thermal-clear"], env=env,
                                   capture_output=True, text=True, timeout=5)
            self.assertEqual(clear.returncode, 0, clear.stderr)
            self.assertIn("old_state=1 requested_state=0 readback=0 max_state=1 rc=0",
                          clear.stdout)
            self.assertEqual((cooler / "cur_state").read_text(), "0\n")

    def test_wired_thermal_override_rejects_ambiguous_or_unsupported_devices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, maximum in (("cooling_device1", "1"),
                                  ("cooling_device2", "1")):
                cooler = root / name
                cooler.mkdir()
                (cooler / "type").write_text("umi-wired-pd\n")
                (cooler / "cur_state").write_text("0\n")
                (cooler / "max_state").write_text(maximum + "\n")
            env = dict(os.environ, TEST_THERMAL_ROOT=str(root))
            result = subprocess.run([self.program, "thermal-stop"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 24)
            for name in ("cooling_device1", "cooling_device2"):
                self.assertEqual((root / name / "cur_state").read_text(), "0\n")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cooler = root / "cooling_device1"
            cooler.mkdir()
            (cooler / "type").write_text("umi-wired-pd\n")
            (cooler / "cur_state").write_text("0\n")
            (cooler / "max_state").write_text("2\n")
            env = dict(os.environ, TEST_THERMAL_ROOT=str(root))
            result = subprocess.run([self.program, "thermal-stop"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 24)
            self.assertEqual((cooler / "cur_state").read_text(), "0\n")

    def test_wired_thermal_write_failure_is_reported_and_state_is_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cooler = root / "cooling_device1"
            cooler.mkdir()
            (cooler / "type").write_text("umi-wired-pd\n")
            (cooler / "cur_state").write_text("0\n")
            (cooler / "max_state").write_text("1\n")
            env = dict(os.environ, TEST_THERMAL_ROOT=str(root),
                       TEST_THERMAL_WRITE_FAIL="1")
            result = subprocess.run([self.program, "thermal-stop"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 24)
            self.assertIn("requested_state=1", result.stdout)
            self.assertNotIn("readback=1", result.stdout)
            self.assertEqual((cooler / "cur_state").read_text(), "0\n")

    def test_informational_probe_messages_and_existing_filters_are_retained(self):
        result = subprocess.run([self.program, "logs"], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        for token in ["dwc3", "USB", "PHY", "UDC", "pending", "PON", "SPMI",
                      "PMIC", "PDC", "PSCI", "failed", "could not", "error", "critical message"]:
            self.assertIn(token, result.stdout)
        self.assertNotIn("unrelated information", result.stdout)


    def test_i2c_report_shows_driver_and_device_tree_compatible(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            device = root / "15-0066"
            (device / "of_node").mkdir(parents=True)
            (device / "of_node/compatible").write_bytes(b"miseed,umi-bq25970-pps\0")
            (device / "driver").symlink_to("../../drivers/umi-bq25970-pps")
            env = dict(os.environ, TEST_I2C_ROOT=str(root))
            result = subprocess.run([self.program, "i2c-report"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("I2C device 15-0066", result.stdout)
            self.assertIn("driver=../../drivers/umi-bq25970-pps", result.stdout)
            self.assertIn("compatible=miseed,umi-bq25970-pps", result.stdout)


    def test_tcpm_log_exports_all_lines_without_a_count_limit(self):
        for count in (1500, 8191, 8200, 20000):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                port = root / "tcpm-test"
                port.mkdir()
                (port / "log").write_text("".join(f"[{i}] frame\n" for i in range(count)))
                env = dict(os.environ, TEST_TCPM_ROOT=str(root))
                result = subprocess.run([self.program, "tcpm-log"], env=env,
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(sum(line.endswith(" frame") for line in result.stdout.splitlines()), count)
                self.assertIn(f"lines={count} complete=1", result.stdout)
                self.assertNotIn("limit reached", result.stdout)


    def test_tcpm_log_reports_advertised_pdos_only_on_explicit_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unrelated").mkdir()
            port = root / "tcpm-c440000.typec"
            port.mkdir()
            (port / "log").write_text(
                "[  0.000000] older record\n" * 150 +
                "[  1.000000] PD state change SNK_READY -> SNK_WAIT_CAPABILITIES\n"
                "[  1.100000] PDO 0: type 3, 5000 mV, 3000 mA [U]\n"
                "[  1.200000] PDO 1: type 3, 3300-11000 mV, 3000 mA\n"
            )
            env = dict(os.environ, TEST_TCPM_ROOT=str(root))
            result = subprocess.run([self.program, "tcpm-log"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("read drains kernel ring", result.stdout)
            self.assertNotIn("unrelated", result.stdout)
            self.assertNotIn("truncated", result.stdout)
            self.assertIn("PDO 1: type 3, 3300-11000 mV, 3000 mA", result.stdout)


    def test_ram_log_grows_without_overwrite_and_replay_does_not_consume(self):
        output = self.run_log("log-ring")
        self.assertEqual(output.count("CHARGE_LOG_BEGIN"), 2)
        self.assertEqual(output.count("first=0 next=11 count=11 storage_errors=0"), 2)
        self.assertEqual(output.count("sample=2\n"), 2)
        self.assertEqual(output.count("sample=10\n"), 2)


    def test_memory_budget_preserves_prefix_and_reports_drops(self):
        output = self.run_log("log-limit")
        self.assertIn("first=0 next=16 count=16 storage_errors=4", output)
        self.assertIn("sample=0\n", output)
        self.assertNotIn("sample=16\n", output)


    def test_rpmh_messages_after_overrun_are_retained(self):
        result = subprocess.run([self.program, "rpmh-overrun"], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ring overrun", result.stderr)
        self.assertIn("rpmh provider ready", result.stdout)


    def test_unavailable_and_incomplete_snapshots_are_explicit(self):
        self.assertIn("recorder_not_started", self.run_log("log-unavailable"))
        output = self.run_log("log-corrupt")
        self.assertIn("snapshot_read_failed", output)
        self.assertNotIn("CHARGE_LOG_BEGIN", output)


    def test_blocked_export_cannot_hold_the_recorder_lock(self):
        self.assertIn("recording continued during blocked USB export",
                      self.run_log("log-blocked-export"))


    def test_kernel_overrun_and_hardware_observer_events_are_retained(self):
        output = self.run_log("log-kernel")
        self.assertIn("kernel_ring_overrun", output)
        self.assertIn("miseed-power usb_present=1 hw_phase=fullon", output)
        self.assertIn("battery temperature unavailable", output)


    def test_supply_samples_preserve_units_and_report_missing_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "battery").mkdir()
            (root / "udc").mkdir()
            (root / "battery/status").write_text("Charging\n")
            (root / "battery/temp").write_text("307\n")
            (root / "battery/current_now").write_text("-120000\n")
            env = dict(os.environ, TEST_POWER_ROOT=str(root), TEST_UDC_DIR=str(root / "udc"))
            output = self.run_log("log-sample", env)
            self.assertIn("status=Charging", output)
            self.assertIn("temp=307", output)
            self.assertIn("current_now=-120000", output)
            self.assertIn("voltage_now=unavailable", output)


    def test_records_during_usb_disconnect_are_exported_after_reconnect(self):
        import pty
        import signal

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            link, phase = root / "tty", root / "phase"
            phase.write_text("host-connected")
            master, slave = pty.openpty()
            link.symlink_to(os.ttyname(slave))
            env = dict(os.environ, TEST_TTY=str(link), TEST_PHASE_FILE=str(phase))
            process = subprocess.Popen([self.program, "console-record"], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            os.close(slave)
            try:
                self.read_until(master, b"miseed> ")
                os.close(master)
                master = None
                phase.write_text("charger-inserted")
                time.sleep(0.25)
                phase.write_text("charger-removed")
                time.sleep(0.2)
                master, slave = pty.openpty()
                link.unlink()
                link.symlink_to(os.ttyname(slave))
                os.close(slave)
                self.read_until(master, b"miseed> ")
                os.write(master, b"charge-log\n")
                output = self.read_until(master, b"CHARGE_LOG_END")
                self.assertIn(b"charger-inserted", output)
                self.assertIn(b"charger-removed", output)
                self.assertIn(b"CHARGE_LOG_BEGIN", output)
                self.assertIsNone(process.poll())
            finally:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=5)
                if master is not None:
                    os.close(master)


if __name__ == "__main__":
    unittest.main()
