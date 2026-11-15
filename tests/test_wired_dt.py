"""Real DTB schema/topology checks; opt in with locally compiled L2 inputs."""
# SPDX-License-Identifier: AGPL-3.0-only
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.check_wired_dt import check, SCHEMAS


@unittest.skipUnless(os.environ.get("MISEED_WIRED_DTB") and os.environ.get("MISEED_DT_SCHEMA")
                     and shutil.which("dt-validate") and shutil.which("fdtget"),
                     "compiled Umi DTB and dtschema required")
class WiredDtTests(unittest.TestCase):
    def setUp(self):
        self.dtb = Path(os.environ["MISEED_WIRED_DTB"])
        self.schema = Path(os.environ["MISEED_DT_SCHEMA"])

    def test_final_dtb_and_supply_relationships(self):
        nodes = check(self.dtb, self.schema)
        self.assertIn("umi_pps", nodes)
        self.assertIn("umi_fg", nodes)

    def test_wrong_target_legacy_supply_and_unsafe_limit_rejected(self):
        nodes = check(self.dtb, self.schema)
        cases = (("umi_charger", "monitored-battery", "0"),
                 ("umi_charger", "power-supplies", "0"),
                 ("umi_pps", "ti,bus-ocp-milliamp", "4000"),
                 ("umi_pps", "monitored-battery", "0"))
        with tempfile.TemporaryDirectory() as directory:
            for owner, prop, value in cases:
                with self.subTest(owner=owner, property=prop):
                    bad = Path(directory) / "bad.dtb"
                    shutil.copyfile(self.dtb, bad)
                    subprocess.run(["fdtput", "-t", "i", str(bad), nodes[owner], prop, value], check=True)
                    with self.assertRaises(ValueError):
                        check(bad, self.schema)

    def test_missing_schema_cannot_silently_pass(self):
        indexed = json.loads(self.schema.read_text())
        del indexed["http://devicetree.org/schemas/" + SCHEMAS[0] + "#"]
        with tempfile.TemporaryDirectory() as directory:
            bad = Path(directory) / "incomplete-schema.json"
            bad.write_text(json.dumps(indexed))
            with self.assertRaisesRegex(ValueError, "schema was not loaded"):
                check(self.dtb, bad)

    def test_fixed_pd_thermal_and_bq_pinctrl_regressions_rejected(self):
        nodes = check(self.dtb, self.schema)
        zone = "/thermal-zones/wired-pd-thermal"
        profiles = nodes["pm8150b_typec"] + "/connector/capabilities/"
        unsafe = subprocess.check_output(
            ["fdtget", "-t", "u", str(self.dtb), profiles + "caps-1", "sink-pdos"],
            text=True).split()
        unsafe[1] = str((int(unsafe[1]) & ~0x3ff) | 300)  # Reject 9 V/3 A.
        cases = ((zone, "thermal-sensors", "0"),
                 (profiles + "caps-1", "sink-pdos", " ".join(unsafe)),
                 (profiles + "caps-0", "sink-pdos", " ".join(unsafe)),
                 (zone + "/trips/warm", "temperature", "46000"),
                 (zone + "/cooling-maps/map-warm", "cooling-device", "0 1 1"),
                 (nodes["pm8150b_typec"] + "/connector", "op-sink-microwatt", "14000000"))
        with tempfile.TemporaryDirectory() as directory:
            for node, prop, value in cases:
                with self.subTest(property=prop):
                    bad = Path(directory) / "bad.dtb"
                    shutil.copyfile(self.dtb, bad)
                    subprocess.run(["fdtput", "-t", "i", str(bad), node, prop,
                                    *value.split()], check=True)
                    with self.assertRaises(ValueError):
                        check(bad, self.schema)
            irq_state = subprocess.check_output(
                ["fdtget", "-t", "s", str(self.dtb), "/__symbols__", "umi_bq_irq_default"],
                text=True).strip()
            bad = Path(directory) / "bad-pinctrl.dtb"
            shutil.copyfile(self.dtb, bad)
            subprocess.run(["fdtput", str(bad), irq_state, "input-enable"], check=True)
            with self.assertRaisesRegex(ValueError, "DT schema diagnostics"):
                check(bad, self.schema)


if __name__ == "__main__":
    unittest.main()
