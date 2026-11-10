"""Passive NFC metadata fixtures; never run PID 1 or open an NFC device."""
# SPDX-License-Identifier: AGPL-3.0-only
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from rescue_test_support import rescue_includes

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static int checked_open(const char *path, int flags, ...) {
    const char *root = getenv("TEST_NFC_ROOT");
    const char *node = getenv("TEST_NFC_NODE");
    if (!strcmp(path, node)) _exit(90);
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND))) _exit(91);
    if (strncmp(path, root, strlen(root)) || path[strlen(root)] != '/') _exit(92);
    return open(path, flags);
}
#define open checked_open
#define main miseed_pid1_main
#include "@SOURCE@"
#undef main
#undef open
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    report_nfc_status(argv[1], argv[2]);
    return 0;
}
"""

@unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "Linux GCC required")
class NfcDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        source = Path(cls.build.name) / "harness.c"
        source.write_text(HARNESS.replace('#include "@SOURCE@"', rescue_includes()))
        cls.binary = Path(cls.build.name) / "nfc-status"
        result = subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(cls.binary)],
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "i2c"
        self.root.mkdir()
        self.node = Path(self.temp.name) / "nfc"
        self.node.mkdir()
        (self.node / "nfc0").mkdir()

    def device(self, name="7-0028", compatible=b"nxp,sn100t\0", bound=True):
        dev = self.root / name
        of = dev / "of_node"
        of.mkdir(parents=True)
        (of / "compatible").write_bytes(compatible)
        (of / "reg").write_bytes(struct.pack(">I", 0x28))
        for prop, gpio in (("enable-gpios", 83), ("firmware-gpios", 110)):
            (of / prop).write_bytes(struct.pack(">III", 0x65, gpio, 0))
        if bound:
            (dev / "driver").symlink_to("../../drivers/sn100x")
        return of

    def run_status(self, character=True, root=None):
        env = dict(os.environ, TEST_NFC_ROOT=str(root or self.root), TEST_NFC_NODE=str(self.node))
        if character:
            env["TEST_CHARACTER"] = "1"
        else:
            env.pop("TEST_CHARACTER", None)
        result = subprocess.run([self.binary, str(root or self.root), str(self.node)],
                                env=env, capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("functionality: not tested", result.stdout)
        self.assertIn("no device open, ioctl, NCI traffic", result.stdout)
        return result.stdout

    def test_dynamic_adapter_and_exact_compatible_list(self):
        for index in (0, 1, 7):
            self.device(f"{index}-0028", b"vendor,board\0nxp,sn100t\0")
        output = self.run_status()
        for index in (0, 1, 7):
            self.assertIn(f"device={index}-0028 driver=sn100x", output)
        for prop, gpio in (("enable-gpios", 83), ("firmware-gpios", 110)):
            self.assertIn(f"{prop}=gpio{gpio}", output)
        self.assertIn("reg=0x28", output)
        self.assertIn("matches=3 scan=finished", output)
        self.assertIn("class device=nfc0", output)
        self.assertIn("Interface: Linux NFC/NCI", output)

    def test_near_match_is_not_an_nfc_device(self):
        self.device(compatible=b"nxp,sn100t-fake\0")
        self.assertIn("matches=0", self.run_status())

    def test_malformed_or_truncated_compatible_is_incomplete(self):
        of = self.device()
        for value in (b"nxp,sn100t", b"nxp,sn100t\0" + b"x" * 600):
            with self.subTest(length=len(value)):
                (of / "compatible").write_bytes(value)
                output = self.run_status()
                self.assertIn("compatible metadata invalid", output)
                self.assertIn("matches=0 scan=incomplete", output)

    def test_unbound_is_not_a_working_controller(self):
        self.device(bound=False)
        self.assertIn("driver=unbound-or-unavailable", self.run_status())

    def test_bad_gpio_and_missing_address_are_reported(self):
        of = self.device()
        (of / "firmware-gpios").write_bytes(b"\0\0\0")
        (of / "reg").unlink()
        output = self.run_status()
        self.assertIn("firmware-gpios=unavailable-or-invalid", output)
        self.assertIn("reg=unavailable-or-invalid", output)
        self.assertIn("enable-gpios=gpio83", output)
        self.assertIn("matches=1 scan=incomplete", output)

    def test_missing_root_and_device(self):
        shutil.rmtree(self.node)
        output = self.run_status(root=self.root / "missing")
        self.assertIn("I2C metadata unavailable", output)
        self.assertIn("scan=incomplete", output)
        self.assertIn(f"class={self.node} unavailable", output)

    def test_only_numeric_class_names_are_reported(self):
        for name in ("nfc", "nfcx", "other", "nfc1junk"):
            (self.node / name).mkdir()
        self.assertIn("class devices=1", self.run_status())

    def test_legacy_compatible_is_not_a_standard_device(self):
        self.device(compatible=b"qcom,nq-nci\0")
        self.assertIn("matches=0", self.run_status())

    def test_fifo_property_does_not_block(self):
        of = self.device()
        (of / "firmware-gpios").unlink()
        os.mkfifo(of / "firmware-gpios")
        self.assertIn("firmware-gpios=unavailable-or-invalid", self.run_status())

    def test_scan_has_a_bound(self):
        for i in range(270):
            (self.root / f"i2c-{i}").mkdir()
        self.assertIn("scan=incomplete", self.run_status())
        self.assertIn("truncated after 256", self.run_status())

if __name__ == "__main__":
    unittest.main()
