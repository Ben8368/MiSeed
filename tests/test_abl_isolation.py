"""Apply real libfdt overlays to prove isolation and phandle preservation."""
# SPDX-License-Identifier: AGPL-3.0-only

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import check_rf_dt
from tools import isolate_abl_targets as isolation


KERNEL = Path(__file__).resolve().parents[1] / "kernel"


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
  tlmm: pinctrl { status = "okay"; gpio-reserved-ranges = <0 4>, <6 1>, <8 26>, <36 8>, <46 2>, <50 18>, <69 14>, <84 26>, <112 43>, <156 25>; };
  mdss_mdp: display { controller { }; };
  sdhc_2: mmc { status = "disabled"; };
  consumer { link = <&spmi_bus &tlmm &mdss_mdp &sdhc_2>; };
 };
 abl_ovf {
  status = "disabled";
  miseed_dtbo_soc: isolated-soc { status = "disabled"; };
  miseed_dtbo_spmi_bus: isolated-spmi-bus { status = "disabled"; };
  miseed_dtbo_tlmm: isolated-tlmm { status = "disabled"; };
  isolated-mdss-mdp { status = "disabled"; miseed_dtbo_mdss_mdp: target { status = "disabled"; }; };
  isolated-sdhc-2 { status = "disabled"; miseed_dtbo_sdhc_2: target { status = "disabled"; }; };
 };
};
""".replace("PMICS", pmics))
        self.overlay = self.compile("overlay", """/dts-v1/; /plugin/;
&spmi_bus { qcom,pm8150@0 { compatible = "qcom,spmi-pmic"; reg = <0 0>; }; };
&mdss_mdp { connectors { vendor-panel { }; }; };
&sdhc_2 { status = "okay"; };
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
        self.assertEqual(isolation.get(merged, "/soc/mmc", "status"), "disabled")
        self.assertNotIn(b"connectors", subprocess.check_output(["fdtget", "-l", str(merged), "/soc/display"]))

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
        with self.assertRaisesRegex(ValueError, "Umi UART12"):
            isolation.verify(self.base)
        isolation.put(self.base, "/abl_ovf", "status", "okay")
        with self.assertRaisesRegex(ValueError, "container must be disabled"):
            isolation.verify(self.base)


    def test_native_board_ranges_keep_rx_enable_and_reserve_unused_rx_irq(self):
        native = (KERNEL / "display/umi-native-dsi.dtsi").read_text(encoding="utf-8")
        board = self.base.with_suffix(".dts").read_text(encoding="utf-8")
        stubs = """
&{/soc} {
 apps_rsc: rsc { };
 vph_pwr: vph { };
 vreg_l5a_0p88: l5a { };
 dispcc: clock { compatible = "qcom,sm8250-dispcc"; #reset-cells = <1>; };
 mdss: mdss { compatible = "qcom,sm8250-mdss"; };
 mdss_dsi0_phy: dsi-phy { };
 mdss_dsi0: dsi { mdss_dsi0_out: endpoint { }; };
};
"""
        for token, value in (("RPMH_REGULATOR_MODE_HPM", "1"), ("DISP_CC_MDSS_CORE_BCR", "0"),
                             ("GPIO_ACTIVE_LOW", "1"), ("GPIO_ACTIVE_HIGH", "0")):
            native = native.replace(token, value)
        dtb = self.compile("native", board + stubs + native)
        isolation.isolate(dtb, native=True)
        self.assertEqual(isolation.get(dtb, "/__symbols__", "miseed_live_tlmm"), "/soc/pinctrl")

    def test_rx_pinctrl_rejects_any_extra_state_or_output_on_power_good(self):
        rx = (KERNEL / "power/umi-rx1619.dtsi").read_text(encoding="utf-8")
        rx = rx.replace("GPIO_ACTIVE_HIGH", "0").replace("GPIO_ACTIVE_LOW", "1")
        board = self.base.with_suffix(".dts").read_text(encoding="utf-8")
        stubs = """
&{/soc} {
 pm8150b_gpios: pmic-gpio { #gpio-cells = <2>; gpio-controller; };
 i2c16: i2c@888000 { #address-cells = <1>; #size-cells = <0>; };
 umi_charger: charger { };
 umi_nfc: nfc@28 {
  compatible = "nxp,sn100t";
  reg = <0x28>;
  interrupts = <0x6f 4>;
  enable-gpios = <&tlmm 83 0>;
  firmware-gpios = <&tlmm 110 0>;
 };
};
"""
        dtb = self.compile("rx", board + stubs + rx)
        isolation.isolate(dtb)
        check_rf_dt.validate(dtb)
        pg = isolation.get(dtb, "/__symbols__", "rx1619_power_good_default")
        rx_node = isolation.get(dtb, "/__symbols__", "umi_wireless_rx")
        pg_handle = self.cells(dtb, pg, "phandle").decode().strip()
        mutated = self.root / "mutated.dtb"
        shutil.copyfile(dtb, mutated)
        subprocess.run(["fdtput", "-t", "x", str(mutated), rx_node, "pinctrl-0",
                        pg_handle, pg_handle], check=True)
        with self.assertRaisesRegex(ValueError, "pinctrl-0"):
            check_rf_dt.validate(mutated)
        subprocess.run(["fdtput", str(dtb), pg, "output-high"], check=True)
        with self.assertRaisesRegex(ValueError, "only configure an input"):
            check_rf_dt.validate(dtb)


if __name__ == "__main__":
    unittest.main()
