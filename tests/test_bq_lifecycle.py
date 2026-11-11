"""Exercise the real BQ callback/IRQ ownership boundary with pthread locks."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


class BqLifecycleTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_exclusive_subscription_irq_and_detach(self):
        source = (ROOT / "kernel/power/bq25970_i2c.c").read_text()
        parts = []
        for prefix in ("int bq25970_subscribe", "void bq25970_unsubscribe", "static irqreturn_t bq25970_irq"):
            begin = source.index(prefix + "(")
            parts.append(source[begin:source.index("\n}\n", begin) + 3])
        harness = (ROOT / "tests/bq_lifecycle_harness.c").read_text().replace("/* CHIP_UNDER_TEST */", "\n".join(parts))
        with tempfile.TemporaryDirectory() as tmp:
            src, binary = Path(tmp) / "chip.c", Path(tmp) / "chip"
            src.write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread", str(src), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)
