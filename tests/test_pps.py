"""Offline tests of the actual guarded PPS controller (no hardware access)."""
# SPDX-License-Identifier: AGPL-3.0-only

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from power_test_support import power_source

ROOT = Path(__file__).resolve().parents[1]


class PpsTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_real_controller_success_faults_deadlines_and_cleanup(self):
        # Compile the real worker and hardware helpers with synthetic transports.
        source = (ROOT / "kernel/power/umi_pps_internal.h").read_text()
        for name in ("bq25970.c", "umi_pps_hw.c", "umi_pps.c"):
            source += "\n" + (ROOT / "kernel/power" / name).read_text()
        full = "\n".join((ROOT / "kernel/power" / name).read_text() for name in
                         ("umi_pps_driver.c", "umi_pps_supply.c", "umi_pps_thermal.c"))
        start = full.index("static int umi_pps_health(")
        source += full[start:full.index("\nstatic enum power_supply_property", start)]
        for name in ("umi_pps_suspend", "umi_pps_resume", "umi_pps_get_property",
                     "umi_pps_set_property", "umi_pps_property_is_writeable",
                     "umi_pps_get_temp", "umi_pps_get_max_state",
                     "umi_pps_get_cur_state", "umi_pps_set_cur_state"):
            start = full.index("static int " + name + "(")
            source += full[start:full.index("\n}\n", start) + 3]
        source = "\n".join(line for line in source.splitlines()
                           if not line.startswith("#include <linux/")
                           and line != '#include "umi_pps_internal.h"'
                           and not line.startswith("extern const struct")
                           and not line.startswith("EXPORT_SYMBOL_GPL"))
        harness = (ROOT / "tests/pps_harness.c").read_text()
        power = re.search(r"op-sink-microwatt = <(\d+)>", (ROOT / "kernel/power/umi-pps.dtsi").read_text()).group(1)
        harness = harness.replace("@OPERATING_SINK_MW@", str(int(power) // 1000))
        harness = harness.replace("/* MISEED_DRIVER_UNDER_TEST */", source)
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "pps.c"
            binary = Path(directory) / ("pps.exe" if os.name == "nt" else "pps")
            entry.write_text(harness)
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function", "-Wno-unused-parameter",
                 "-I", str(ROOT / "kernel/power"), "-I", str(ROOT / "tests"),
                 str(entry), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_actual_smb5_exclusive_ownership_and_failure_retention(self):
        source = power_source("qcom_pm8150b_charger")
        parts = []
        for name in ("static bool smb5_is_ours", "int umi_smb5_claim",
                     "int umi_smb5_check", "int umi_smb5_release"):
            start = source.index(name + "(")
            end = source.index("\n}\n", start) + 3
            parts.append(source[start:end])
        harness = (ROOT / "tests/pps_smb5_harness.c").read_text().replace(
            "/* MISEED_SMB5_UNDER_TEST */", "\n".join(parts))
        with tempfile.TemporaryDirectory() as directory:
            entry, binary = Path(directory) / "smb5.c", Path(directory) / "smb5"
            entry.write_text(harness)
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", str(entry), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_sink_only_pd_pps_nodes_are_enabled(self):
        board = (ROOT / "kernel/power/umi-pps.dtsi").read_text()
        self.assertEqual(board.count('status = "okay"'), 5)
        self.assertRegex(board, r"&qupv3_id_2\s*\{\s*status = \"okay\";")
        self.assertNotIn('status = "disabled"', board)
        for unknown in ("ti,sense-resistor-micro-ohms", "ti,bat-ocp-milliamp",
                        "ti,tsbat-threshold-code", "ti,tsbus-threshold-code",
                        "ti,die-alarm-decicelsius", "miseed,die-max-decicelsius"):
            self.assertNotIn(unknown + " =", board)
        self.assertIn('power-role = "sink"', board)
        self.assertNotIn("source-pdos", board)
        self.assertIn('PDO_FIXED(5000, 3000,', board)

    def test_reference_files_are_hash_locked(self):
        import json
        lock = json.loads((ROOT / "manifests/sources.lock.json").read_text())
        source = next(x for x in lock["sources"] if x["id"] == "lineage-umi-power-runtime-reference")
        self.assertEqual(source["revision"], "71b13e62f057a649b77fe4062feb73ee72ad609c")
        for path in ("drivers/power/supply/ti/bq25970_reg.h",
                     "drivers/power/supply/ti/bq2597x_charger.c",
                     "drivers/power/supply/ti/pd_policy_manager.c",
                     "drivers/power/supply/ti/pd_policy_manager.h",
                     "arch/arm64/boot/dts/vendor/qcom/umi-pinctrl.dtsi"):
            self.assertIn(path, source["included_paths"])
            self.assertRegex(source["reviewed_sha256"][path], r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
