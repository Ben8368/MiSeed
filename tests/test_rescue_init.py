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
HARNESS = r"""
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define CHARGE_LOG_CAPACITY 8

static int test_open(const char *path, int flags, ...);
static DIR *test_opendir(const char *path);
static int test_stat(const char *path, struct stat *st);
static ssize_t test_read(int fd, void *buffer, size_t size);
static unsigned int test_sleep(unsigned int seconds);
static long test_reboot_syscall(long number, ...);
#define open test_open
#define opendir test_opendir
#define stat(...) test_stat(__VA_ARGS__)
#define read test_read
#define sleep test_sleep
#define syscall test_reboot_syscall
#define main miseed_pid1_main
#include "@SOURCE@"
#undef main
#undef open
#undef opendir
#undef stat
#undef read
#undef sleep
#undef syscall

static int kmsg_fd = -1;
static int delayed_udc;
static int retries;
static int missing_pon;
static int reboot_calls;
static int kmsg_overrun;

static long test_reboot_syscall(long number, ...) {
    (void)number;
    reboot_calls++;
    errno = EPERM;
    return -1;
}

static const char *pps_prefix = "/sys/bus/i2c/drivers/umi-bq25970-pps";
static int test_stat(const char *path, struct stat *st) {
    char mapped[1024];
    if (getenv("TEST_PPS_ROOT") && !strncmp(path, pps_prefix, strlen(pps_prefix))) {
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_PPS_ROOT"), path + strlen(pps_prefix));
        return stat(mapped, st);
    }
    if (getenv("TEST_TCPM_ROOT") && !strncmp(path, "/sys/kernel/debug/usb/tcpm", 26)) {
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_TCPM_ROOT"), path + 26);
        return stat(mapped, st);
    }
    return stat(path, st);
}

static int test_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    if (!strcmp(path, "/dev/ttyGS0")) path = getenv("TEST_TTY");
    if (!strcmp(path, "/sys/kernel/config/usb_gadget/miseed/UDC"))
        path = getenv("TEST_UDC_BIND");
    if (!strcmp(path, "/dev/kmsg")) {
        kmsg_fd = open("/dev/null", flags);
        return kmsg_fd;
    }
    if (!strncmp(path, "/sys/class/power_supply/", 24) && getenv("TEST_POWER_ROOT")) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s/%s", getenv("TEST_POWER_ROOT"), path + 24);
        return open(mapped, flags, mode);
    }
    if (getenv("TEST_I2C_ROOT") && !strncmp(path, "/sys/bus/i2c/devices/", 21)) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s/%s", getenv("TEST_I2C_ROOT"), path + 21);
        return open(mapped, flags, mode);
    }
    if (getenv("TEST_PPS_ROOT") && !strncmp(path, pps_prefix, strlen(pps_prefix))) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_PPS_ROOT"), path + strlen(pps_prefix));
        return open(mapped, flags, mode);
    }
    if (getenv("TEST_TCPM_ROOT") && !strncmp(path, "/sys/kernel/debug/usb/tcpm", 26)) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_TCPM_ROOT"), path + 26);
        return open(mapped, flags, mode);
    }
    return open(path, flags, mode);
}

static DIR *test_opendir(const char *path) {
    if (missing_pon && !strcmp(path, "/sys/bus/platform/devices")) {
        errno = ENOENT;
        return NULL;
    }
    if (!strcmp(path, "/sys/class/udc")) path = getenv("TEST_UDC_DIR");
    if (!path) { errno = ENOENT; return NULL; }
    if (!strcmp(path, "/sys/class/power_supply") && getenv("TEST_POWER_ROOT"))
        path = getenv("TEST_POWER_ROOT");
    if (getenv("TEST_I2C_ROOT") && !strcmp(path, "/sys/bus/i2c/devices"))
        path = getenv("TEST_I2C_ROOT");
    if (getenv("TEST_PPS_ROOT") && !strcmp(path, pps_prefix)) path = getenv("TEST_PPS_ROOT");
    if (getenv("TEST_TCPM_ROOT") && !strcmp(path, "/sys/kernel/debug/usb/tcpm"))
        path = getenv("TEST_TCPM_ROOT");
    return opendir(path);
}

static ssize_t test_read(int fd, void *buffer, size_t size) {
    static const char *records[] = {
        "6,1,0,-;dwc3 controller ready\n",
        "6,2,0,-;USB connection ready\n",
        "6,3,0,-;88e3000.PHY ready\n",
        "6,4,0,-;UDC registered\n",
        "6,5,0,-;sync_state() pending due to supplier\n",
        "6,6,0,-;PON ready\n",
        "6,7,0,-;SPMI ready\n",
        "6,8,0,-;PMIC ready\n",
        "6,9,0,-;unrelated failed\n",
        "6,10,0,-;unrelated could not start\n",
        "6,11,0,-;unrelated error\n",
        "3,12,0,-;critical message\n",
        "6,13,0,-;unrelated information\n",
        "6,14,0,-;PDC ready\n",
        "6,15,0,-;PSCI ready\n",
        "6,16,0,-;miseed-power usb_present=1 hw_phase=fullon\n",
        "6,17,0,-;battery temperature unavailable\n",
    };
    static size_t index;
    if (fd == kmsg_fd) {
        size_t length;
        if (kmsg_overrun) { kmsg_overrun = 0; errno = EPIPE; return -1; }
        if (index == sizeof(records) / sizeof(records[0])) {
            errno = EAGAIN;
            return -1;
        }
        length = strlen(records[index]);
        if (length > size) abort();
        memcpy(buffer, records[index++], length);
        return (ssize_t)length;
    }
    return read(fd, buffer, size);
}

static unsigned int test_sleep(unsigned int seconds) {
    if (delayed_udc) {
        char path[1024];
        int fd;
        if (++retries > 3) exit(9);
        if (retries == 1) {
            snprintf(path, sizeof(path), "%s/a600000.usb", getenv("TEST_UDC_DIR"));
            fd = open(path, O_WRONLY | O_CREAT, 0600);
        } else {
            fd = open(getenv("TEST_UDC_BIND"), O_WRONLY | O_CREAT, 0600);
        }
        if (fd < 0) exit(10);
        close(fd);
        return 0;
    }
    return sleep(seconds);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "logs")) report_usb_kernel_messages();
    else if (!strcmp(argv[1], "i2c-report")) report_i2c_devices();
    else if (!strcmp(argv[1], "tcpm-log")) report_tcpm_logs();
    else if (!strcmp(argv[1], "bind")) {
        delayed_udc = 1;
        wait_for_usb_acm();
        if (retries != 2) return 3;
    } else if (!strcmp(argv[1], "pps-discovery")) {
        char path[512];
        int ret = pps_control_path(path, sizeof(path));
        printf("discovery=%d errno=%d\n", ret, errno);
        if (!ret && pps_control(NULL)) return 23;
    } else if (!strcmp(argv[1], "console")) usb_console();
    else if (!strcmp(argv[1], "console-record")) {
        pid_t child;
        char previous[128] = { 0 };
        if (charge_log_init()) return 10;
        charge_log_append("START", "session");
        child = fork();
        if (child < 0) return 11;
        if (!child) usb_console();
        for (;;) {
            char state[128];
            if (!charge_read_value(getenv("TEST_PHASE_FILE"), state, sizeof(state)) &&
                strcmp(state, previous)) {
                charge_log_append("SAMPLE", "%s", state);
                strcpy(previous, state);
            }
            usleep(100000);
        }
    }
    else if (!strcmp(argv[1], "log-ring")) {
        if (charge_log_init()) return 12;
        for (int i = 0; i < CHARGE_LOG_CAPACITY + 3; i++)
            charge_log_append("SAMPLE", "sample=%d", i);
        charge_log_export(0);
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-unavailable")) charge_log_export(0);
    else if (!strcmp(argv[1], "log-corrupt")) {
        if (charge_log_init()) return 13;
        charge_log_append("SAMPLE", "before truncation");
        if (ftruncate(charge_log_fd, sizeof(struct charge_log_header))) return 14;
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-sample")) {
        if (charge_log_init()) return 15;
        charge_snapshot();
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-kernel")) {
        if (charge_log_init()) return 16;
        charge_kmsg_fd = test_open("/dev/kmsg", O_RDONLY);
        kmsg_overrun = 1;
        charge_kernel_drain();
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-blocked-export")) {
        int pipefd[2], status;
        pid_t child;
        char message[680];
        if (charge_log_init()) return 17;
        memset(message, 'x', sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        for (int i = 0; i < CHARGE_LOG_CAPACITY; i++) charge_log_append("SAMPLE", "%s", message);
        if (pipe(pipefd) || fcntl(pipefd[1], F_SETPIPE_SZ, 4096) < 0) return 18;
        child = fork();
        if (child < 0) return 19;
        if (!child) {
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(20);
            setvbuf(stdout, NULL, _IONBF, 0);
            charge_log_export(0);
            _exit(0);
        }
        close(pipefd[1]);
        usleep(50000);
        alarm(2);
        charge_log_append("SAMPLE", "recording continued during blocked USB export");
        alarm(0);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(pipefd[0]);
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "bootloader-unbound")) {
        missing_pon = 1;
        request_bootloader();
        if (reboot_calls) return 5;
    }
    else return 4;
    return 0;
}
"""


@unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"),
                     "Linux GCC and pseudo-terminals required")
class RescueInitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        root = Path(cls.build.name)
        harness = root / "harness.c"
        harness.write_text(HARNESS.replace("@SOURCE@", (ROOT / "kernel/rescue_init.c").as_posix()))
        cls.program = root / "harness"
        result = subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror",
                                 str(harness), "-o", str(cls.program)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

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
            self.assertIn("umi-bq25970-pps", result.stdout)

    def test_tcpm_log_reports_advertised_pdos_only_on_explicit_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            port = root / "tcpm-c440000.typec"
            port.mkdir()
            (port / "log").write_text(
                "[  1.000000] PD state change SNK_READY -> SNK_WAIT_CAPABILITIES\n"
                "[  1.100000] PDO 0: type 3, 5000 mV, 3000 mA [U]\n"
                "[  1.200000] PDO 1: type 3, 3300-11000 mV, 3000 mA\n"
            )
            env = dict(os.environ, TEST_TCPM_ROOT=str(root))
            result = subprocess.run([self.program, "tcpm-log"], env=env,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("read drains kernel ring", result.stdout)
            self.assertIn("PDO 1: type 3, 3300-11000 mV, 3000 mA", result.stdout)

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

    def read_until(self, fd, expected, timeout=6):
        received = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not select.select([fd], [], [], 0.1)[0]:
                continue
            try:
                data = os.read(fd, 4096)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                time.sleep(0.01)
                continue
            received += data
            if expected in received:
                return received
        self.fail(f"console did not emit {expected!r}: {received[-500:]!r}")

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

    def run_log(self, command, env=None):
        result = subprocess.run([self.program, command], env=env, capture_output=True,
                                text=True, timeout=6)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_charge_commands_select_explicit_duration_and_stop(self):
        import pty

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            supply = root / "pm8150b-charger"
            supply.mkdir()
            control = supply / "charge_test"
            control.touch()
            master, slave = pty.openpty()
            env = dict(os.environ, TEST_TTY=os.ttyname(slave), TEST_POWER_ROOT=str(root))
            process = subprocess.Popen([self.program, "console"], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            os.close(slave)
            try:
                banner = self.read_until(master, b"miseed> ")
                self.assertIn(b"read-only samples", banner)
                self.assertIn(b"charge-test-10min (600 s)", banner)
                for command, value, message in (
                    (b"charge-test\n", "arm", b"after 60 seconds"),
                    (b"charge-test-10min\n", "arm-10min", b"after 600 seconds"),
                    (b"charge-stop\n", "stop", b"test cancelled"),
                ):
                    control.write_text("")
                    os.write(master, command)
                    self.read_until(master, message)
                    self.assertEqual(control.read_text(), value)
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
            control = first / "fastcharge_test"
            control.touch()
            self.assertIn("discovery=0", self.run_log("pps-discovery", env))
            self.assertEqual(control.read_text(), "")
            second = root / "16-0066"
            second.mkdir()
            (second / "fastcharge_test").touch()
            control.write_text("keep")
            self.assertIn("discovery=-1", self.run_log("pps-discovery", env))
            self.assertEqual(control.read_text(), "keep")

    def test_pps_console_automatic_status_and_unified_stop(self):
        import pty

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pump = root / "pps" / "15-0066"
            pump.mkdir(parents=True)
            control = pump / "fastcharge_test"
            control.touch()
            supply = root / "pm8150b-charger"
            supply.mkdir()
            normal = supply / "charge_test"
            normal.touch()
            master, slave = pty.openpty()
            env = dict(os.environ, TEST_TTY=os.ttyname(slave), TEST_POWER_ROOT=str(root),
                       TEST_PPS_ROOT=str(root / "pps"))
            process = subprocess.Popen([self.program, "console"], env=env,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, start_new_session=True)
            os.close(slave)
            try:
                self.read_until(master, b"miseed> ")
                for command, value, message in (
                    (b"pps-status\n", "", b"miseed> "),
                    (b"pps-stop\n", "stop", b"PPS stopped; SMB5 fallback policy resumed."),
                    (b"charge-stop\n", "stop", b"test cancelled"),
                ):
                    control.write_text("")
                    os.write(master, command)
                    self.read_until(master, message)
                    self.assertEqual(control.read_text(), value)
                self.assertEqual(normal.read_text(), "stop")
            finally:
                process.terminate()
                process.wait(timeout=5)
                os.close(master)

    def test_ram_ring_reports_overwrite_and_replay_does_not_consume(self):
        output = self.run_log("log-ring")
        self.assertEqual(output.count("CHARGE_LOG_BEGIN"), 2)
        self.assertEqual(output.count("first=3 next=11 count=8 overwritten=3"), 2)
        self.assertNotIn("sample=2\n", output)
        self.assertEqual(output.count("sample=10\n"), 2)

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
