"""Read-only framebuffer export: pixels, padded stride, identity and bounds."""
# SPDX-License-Identifier: AGPL-3.0-only
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cc"), "Linux C headers required")
class FramebufferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "framebuffer"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/framebuffer_harness.c"), "-o", str(cls.binary)],
                       capture_output=True, check=True)

    def run_case(self, mode):
        result = subprocess.run([str(self.binary), str(mode)], capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_rgb_padded_stride_dimensions_and_interrupted_read(self):
        for mode in (0, 13):
            text = self.run_case(mode)
            self.assertIn("stride=4352", text)
            self.assertIn("atomic=0 orientation=physical", text)
            rows = [x for x in text.splitlines() if x.startswith("FRAMEBUFFER_ROW")]
            self.assertEqual(len(rows), 293)
            self.assertTrue(rows[0].startswith("FRAMEBUFFER_ROW y=0 rgb=123456ffffff"))
            self.assertTrue(rows[-1].startswith("FRAMEBUFFER_ROW y=2336 rgb="))
            self.assertIn("FRAMEBUFFER_COMPLETE rows=293", text)

    def test_wrong_identity_format_offset_stride_bounds_and_io_fail_closed(self):
        for mode in range(1, 13):
            with self.subTest(mode=mode):
                text = self.run_case(mode)
                self.assertIn("FRAMEBUFFER_ERROR", text)
                self.assertNotIn("FRAMEBUFFER_COMPLETE", text)
