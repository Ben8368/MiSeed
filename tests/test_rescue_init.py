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

static int test_open(const char *path, int flags, ...);
static DIR *test_opendir(const char *path);
static ssize_t test_read(int fd, void *buffer, size_t size);
static unsigned int test_sleep(unsigned int seconds);
static long test_reboot_syscall(long number, ...);
#define open test_open
#define opendir test_opendir
#define read test_read
#define sleep test_sleep
#define syscall test_reboot_syscall
#define main miseed_pid1_main
#include "@SOURCE@"
#undef main
#undef open
#undef opendir
#undef read
#undef sleep
#undef syscall

static int kmsg_fd = -1;
static int delayed_udc;
static int retries;
static int missing_pon;
static int reboot_calls;

static long test_reboot_syscall(long number, ...) {
    (void)number;
    reboot_calls++;
    errno = EPERM;
    return -1;
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
    return open(path, flags, mode);
}

static DIR *test_opendir(const char *path) {
    if (missing_pon && !strcmp(path, "/sys/bus/platform/devices")) {
        errno = ENOENT;
        return NULL;
    }
    if (!strcmp(path, "/sys/class/udc")) path = getenv("TEST_UDC_DIR");
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
    };
    static size_t index;
    if (fd == kmsg_fd) {
        size_t length;
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
    else if (!strcmp(argv[1], "bind")) {
        delayed_udc = 1;
        wait_for_usb_acm();
        if (retries != 2) return 3;
    } else if (!strcmp(argv[1], "console")) usb_console();
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
        subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(harness), "-o", str(cls.program)], check=True, capture_output=True)

    def test_informational_probe_messages_and_existing_filters_are_retained(self):
        result = subprocess.run([self.program, "logs"], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        for token in ["dwc3", "USB", "PHY", "UDC", "pending", "PON", "SPMI",
                      "PMIC", "PDC", "PSCI", "failed", "could not", "error", "critical message"]:
            self.assertIn(token, result.stdout)
        self.assertNotIn("unrelated information", result.stdout)

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


if __name__ == "__main__":
    unittest.main()
