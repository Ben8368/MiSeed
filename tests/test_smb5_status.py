"""Exercise the actual wired PM8150B status decoder with register snapshots."""

# SPDX-License-Identifier: AGPL-3.0-only

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Smb5StatusTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_wired_status_phases_faults_and_io_recovery(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_hw.c").read_text()
        internal = (ROOT / "kernel/power/qcom_pm8150b_internal.h").read_text()
        phases = re.search(r"enum charger_status \{.*?\n\};", internal, re.S).group()
        functions = []
        for name in ("smb5_get_prop_usb_online", "smb5_get_prop_status"):
            start = source.index("int " + name + "(")
            functions.append(source[start:source.index("\n}\n", start) + 3])
        harness = (ROOT / "tests/smb5_status_harness.c").read_text().replace(
            "/* MISEED_SMB5_STATUS_UNDER_TEST */", phases + "\n" + "\n".join(functions))
        with tempfile.TemporaryDirectory() as directory:
            entry, binary = Path(directory) / "status.c", Path(directory) / "status"
            entry.write_text(harness)
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I",
                 str(ROOT / "kernel/power"), str(entry), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
