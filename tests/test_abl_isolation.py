"""Apply real libfdt overlays to prove isolation and phandle preservation."""
# SPDX-License-Identifier: AGPL-3.0-only

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import isolate_abl_targets as isolation


@unittest.skipUnless(all(shutil.which(x) for x in ("dtc", "fdtget", "fdtput", "fdtoverlay")),
                     "device-tree compiler and libfdt tools required")
class OverlayIsolationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        pmics = "".join(f"pmic@{i} {{ reg = <0x{i} 0>; }};" for i in "012345ab")
        self.base = self.compile("base", """/dts-v1/;
/ {
 soc: soc {
  compatible = "simple-bus";
  spmi_bus: spmi { #address-cells = <2>; #size-cells = <0>; PMICS };
  tlmm: pinctrl { status = "okay"; gpio-reserved-ranges = <0 34>, <36 8>, <46 22>, <69 112>; };
  consumer { link = <&spmi_bus &tlmm>; };
 };
 abl_ovf {
  status = "disabled";
  miseed_dtbo_soc: isolated-soc { status = "disabled"; };
  miseed_dtbo_spmi_bus: isolated-spmi-bus { status = "disabled"; };
  miseed_dtbo_tlmm: isolated-tlmm { status = "disabled"; };
 };
};
""".replace("PMICS", pmics))
        self.overlay = self.compile("overlay", """/dts-v1/; /plugin/;
&spmi_bus { qcom,pm8150@0 { compatible = "qcom,spmi-pmic"; reg = <0 0>; }; };
&soc { injected-consumer { compatible = "gpio-keys"; }; };
&tlmm { gpio-reserved-ranges = <28 4>; vendor-state { pins = "gpio68"; }; };
""")

    def compile(self, name, source):
        dts = self.root / (name + ".dts")
        dtb = dts.with_suffix(".dtb")
        dts.write_text(source, encoding="utf-8")
        subprocess.run(["dtc", "-q", "-@", "-I", "dts", "-O", "dtb",
                        "-o", str(dtb), str(dts)], check=True, capture_output=True)
        return dtb

    def apply(self):
        output = self.root / "merged.dtb"
        subprocess.run(["fdtoverlay", "-i", str(self.base), "-o", str(output),
                        str(self.overlay)], check=True)
        return output

    def cells(self, dtb, node, prop):
        return subprocess.check_output(["fdtget", "-t", "x", str(dtb), node, prop]).replace(b"\r\n", b"\n")

    def test_vendor_injection_reproduces_duplicate_without_isolation(self):
        merged = self.apply()
        children = subprocess.check_output(["fdtget", "-l", str(merged), "/soc/spmi"])
        self.assertIn(b"qcom,pm8150@0", children)
        self.assertEqual(self.cells(merged, "/soc/pinctrl", "gpio-reserved-ranges"), b"1c 4\n")

    def test_isolation_preserves_live_phandles_and_blocks_vendor_consumers(self):
        original = self.cells(self.base, "/soc/consumer", "link")
        isolation.isolate(self.base)
        merged = self.apply()
        isolation.verify(merged)
        self.assertEqual(self.cells(merged, "/soc/consumer", "link"), original)
        self.assertEqual(self.cells(merged, "/soc/spmi", "phandle"),
                         self.cells(self.base, "/soc/spmi", "phandle"))
        self.assertEqual(isolation.get(merged, "/abl_ovf/isolated-spmi-bus/qcom,pm8150@0",
                                      "compatible"), "qcom,spmi-pmic")
        live = subprocess.check_output(["fdtget", "-l", str(merged), "/soc"])
        self.assertNotIn(b"injected-consumer", live)

    def test_refuses_enabled_quarantine_before_mutating(self):
        isolation.put(self.base, "/abl_ovf/isolated-tlmm", "status", "okay")
        before = self.base.read_bytes()
        with self.assertRaisesRegex(ValueError, "not disabled"):
            isolation.isolate(self.base)
        self.assertEqual(before, self.base.read_bytes())

    def test_failed_final_verification_leaves_original_unchanged(self):
        isolation.put(self.base, "/abl_ovf", "status", "okay")
        before = self.base.read_bytes()
        with self.assertRaisesRegex(ValueError, "container must be disabled"):
            isolation.isolate(self.base)
        self.assertEqual(before, self.base.read_bytes())

    def test_rejects_escaped_vendor_target(self):
        isolation.isolate(self.base)
        isolation.put(self.base, "/__symbols__", "spmi_bus", "/soc/spmi")
        with self.assertRaisesRegex(ValueError, "escaped"):
            isolation.verify(self.apply())

    def test_rejects_direct_gpio_override_and_enabled_container(self):
        isolation.isolate(self.base)
        subprocess.run(["fdtput", "-t", "u", str(self.base), "/soc/pinctrl",
                        "gpio-reserved-ranges", "28", "4"], check=True)
        with self.assertRaisesRegex(ValueError, "only Umi UART12"):
            isolation.verify(self.base)
        isolation.put(self.base, "/abl_ovf", "status", "okay")
        with self.assertRaisesRegex(ValueError, "container must be disabled"):
            isolation.verify(self.base)


if __name__ == "__main__":
    unittest.main()
