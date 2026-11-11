
# Actual libfdt overlays, exported-FDT framing and protected topology.
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from tools import check_runtime_dt as audit
from tools import isolate_abl_targets as isolation


@unittest.skipUnless(all(shutil.which(x) for x in ("dtc", "fdtget", "fdtput", "fdtoverlay")), "libfdt tools required")
class RuntimeDtTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        pmics = "".join(f"pmic@{i} {{ reg = <0x{i} 0>; }};" for i in "012345ab")
        self.base = self.compile("base", '''/dts-v1/;
/ { #address-cells = <2>; #size-cells = <2>; compatible = "xiaomi,umi";
 memory@80000000 { device_type = "memory"; reg = <0 0x80000000 0 0x40000000>; };
 aliases { serial0 = "/soc/serial"; };
 soc: soc { compatible = "simple-bus";
  spmi_bus: spmi { #address-cells = <2>; #size-cells = <0>; PMICS };
  tlmm: pinctrl { status = "okay"; gpio-reserved-ranges = <0 4>, <6 1>, <8 26>, <36 8>, <46 22>, <69 14>, <84 26>, <112 69>; };
  serial { };
  mdss_mdp: display { controller { }; };
  sdhc_2: mmc { status = "disabled"; };
 };
 abl_ovf { status = "disabled";
  miseed_dtbo_soc: isolated-soc { status = "disabled"; };
  miseed_dtbo_spmi_bus: isolated-spmi-bus { status = "disabled"; };
  miseed_dtbo_tlmm: isolated-tlmm { status = "disabled"; };
  isolated-mdss-mdp { status = "disabled"; miseed_dtbo_mdss_mdp: target { status = "disabled"; }; };
  isolated-sdhc-2 { status = "disabled"; miseed_dtbo_sdhc_2: target { status = "disabled"; }; };
 };
};'''.replace("PMICS", pmics))
        isolation.isolate(self.base)
        self.reference = self.base.read_bytes()

    def compile(self, name, text):
        source, target = self.root / (name + ".dts"), self.root / (name + ".dtb")
        source.write_text(text)
        subprocess.run(["dtc", "-q", "-@", "-I", "dts", "-O", "dtb", "-o", str(target), str(source)], check=True, capture_output=True)
        return target

    def overlay(self, fragment):
        overlay = self.compile("overlay", "/dts-v1/; /plugin/; / { " + fragment + " };")
        result = self.root / "merged.dtb"
        subprocess.run(["fdtoverlay", "-i", str(self.base), "-o", str(result), str(overlay)], check=True)
        return result.read_bytes()

    def test_symbol_target_is_contained(self):
        blob = self.overlay('fragment@0 { target = <&spmi_bus>; __overlay__ { qcom,pm8150@0 { reg = <0 0>; }; }; };')
        self.assertTrue(audit.audit(self.reference, blob)["result"].startswith("PASS"))

    def test_target_path_duplicate_is_rejected(self):
        blob = self.overlay('fragment@0 { target-path = "/soc/spmi"; __overlay__ { qcom,pm8150@0 { reg = <0 0>; }; }; };')
        with self.assertRaisesRegex(ValueError, "subtree changed"):
            audit.audit(self.reference, blob)

    def test_numeric_phandle_override_is_rejected(self):
        handle = audit.cells(audit.parse(self.reference)["/soc/spmi"]["phandle"])[0]
        blob = self.overlay(f'fragment@0 {{ target = <{handle}>; __overlay__ {{ #address-cells = <1>; }}; }};')
        with self.assertRaisesRegex(ValueError, "subtree changed"):
            audit.audit(self.reference, blob)

    def test_direct_soc_consumer_and_alias_are_rejected(self):
        for fragment, message in [
            ('fragment@0 { target-path = "/soc"; __overlay__ { vendor-device { }; }; };', "topology"),
            ('fragment@0 { target-path = "/aliases"; __overlay__ { serial0 = "/abl_ovf/isolated-soc"; }; };', "aliases")]:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                audit.audit(self.reference, self.overlay(fragment))

    def test_dpu_mmc_symbol_targets_are_contained(self):
        blob = self.overlay('fragment@0 { target = <&mdss_mdp>; __overlay__ { connectors { vendor-panel { }; }; }; };'
                            'fragment@1 { target = <&sdhc_2>; __overlay__ { status = "okay"; }; };')
        self.assertTrue(audit.audit(self.reference, blob)["result"].startswith("PASS"))

    def test_deep_soc_and_mmc_changes_cannot_hide_behind_root_fix(self):
        for path, content in [("/soc/display/controller", "vendor-child { };") ,
                              ("/soc/mmc", 'status = "okay";'),
                              ("/soc/serial", "nested { leaf { }; };")]:
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "subtree changed"):
                audit.audit(self.reference, self.overlay(
                    f'fragment@0 {{ target-path = "{path}"; __overlay__ {{ {content} }}; }};'))

    def test_root_identity_is_not_allowlisted(self):
        for property in ['compatible = "qcom,kona-mtp";', 'model = "Qualcomm xiaomi umi";']:
            with self.subTest(property=property), self.assertRaisesRegex(ValueError, "root hardware identity"):
                audit.audit(self.reference, self.overlay(
                    f'fragment@0 {{ target-path = "/"; __overlay__ {{ {property} }}; }};'))

    def test_root_changes_do_not_hide_live_hardware_changes(self):
        blob = self.overlay('fragment@0 { target-path = "/"; __overlay__ { compatible = "qcom,kona"; model = "vendor model"; }; };'
                            'fragment@1 { target-path = "/soc/display/controller"; __overlay__ { injected { }; }; };')
        report = audit.audit_report(self.reference, blob)
        self.assertEqual(report["result"], "FAIL")
        self.assertEqual([i["section"] for i in report["issues"]],
                         ["root_identity", "root_identity", "live_hardware"])
        with self.assertRaisesRegex(ValueError, "compatible.*model.*subtree changed"):
            audit.audit(self.reference, blob)

    def test_board_selection_and_interrupt_root_properties_are_protected(self):
        for prop in ("qcom,board-id", "qcom,msm-id", "interrupt-parent"):
            with self.subTest(prop=prop), self.assertRaisesRegex(ValueError, prop):
                audit.audit(self.reference, self.overlay(
                    f'fragment@0 {{ target-path = "/"; __overlay__ {{ {prop} = <99>; }}; }};'))

    def test_vendor_root_metadata_copy_is_outside_fragment_isolation(self):
        # ABL's LibUfdt copies existing top-level properties before fragments.
        # Plain libfdt ignores these metadata properties. Emulate that extra
        # step explicitly; the independent real LibUfdt replay is archived.
        overlay = self.compile("vendor-metadata", '/dts-v1/; /plugin/; / { compatible = "qcom,kona"; fragment@0 { target = <&mdss_mdp>; __overlay__ { child { }; }; }; };')
        result = self.root / "vendor-merged.dtb"
        subprocess.run(["fdtoverlay", "-i", str(self.base), "-o", str(result), str(overlay)], check=True)
        self.assertTrue(audit.audit(self.reference, result.read_bytes())["result"].startswith("PASS"))
        isolation.put(result, "/", "compatible", "qcom,kona")
        report = audit.audit_report(self.reference, result.read_bytes())
        self.assertEqual([i["section"] for i in report["issues"]], ["root_identity"])

    def test_nested_quarantine_cannot_be_enabled(self):
        blob = self.overlay('fragment@0 { target-path = "/abl_ovf/isolated-sdhc-2"; __overlay__ { status = "okay"; }; };')
        with self.assertRaisesRegex(ValueError, "enabled quarantine"):
            audit.audit(self.reference, blob)

    def test_numeric_dpu_reference_still_targets_live_node_and_is_rejected(self):
        handle = audit.cells(audit.parse(self.reference)["/soc/display"]["phandle"])[0]
        blob = self.overlay(f'fragment@0 {{ target = <{handle}>; __overlay__ {{ child {{ }}; }}; }};')
        with self.assertRaisesRegex(ValueError, "subtree changed"):
            audit.audit(self.reference, blob)

    def test_zero_ram_is_rejected(self):
        blob = self.overlay('fragment@0 { target-path = "/memory@80000000"; __overlay__ { reg = <0 0x80000000 0 0>; }; };')
        with self.assertRaisesRegex(ValueError, "RAM"):
            audit.audit(self.reference, blob)

    def test_export_roundtrip_and_truncation(self):
        blob = self.reference
        lines = [f"DTB_BEGIN {len(blob)}"] + [f"DTB {i:08x} {blob[i:i+32].hex()}" for i in range(0, len(blob), 32)] + ["DTB_END"]
        self.assertEqual(audit.decode_export("\n".join(lines)), blob)
        for altered in (lines[:-1], lines[:2] + lines[3:], lines + lines):
            with self.assertRaises(ValueError):
                audit.decode_export("\n".join(altered))

    def test_parser_rejects_invalid_bounds_and_truncation(self):
        for offset, value in ((4, len(self.reference) + 1), (8, 0xfffffff0), (12, 0xfffffff0), (16, 0xfffffff8)):
            blob = bytearray(self.reference)
            struct.pack_into(">I", blob, offset, value)
            with self.assertRaises(ValueError):
                audit.parse(bytes(blob))
        with self.assertRaises(ValueError):
            audit.parse(self.reference[:-4])
