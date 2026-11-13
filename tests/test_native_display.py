"""Compile actual panel lifecycle against a synthetic DSI transport."""
# SPDX-License-Identifier: AGPL-3.0-only
import re
import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import apply_display_port as port

ROOT = Path(__file__).resolve().parents[1]


class NativePanelTests(unittest.TestCase):
    def test_dsi_diagnostics_staging_refuses_modified_and_symlink_sources(self):
        original = "\n".join(port.DSI_INSERTIONS).encode()
        with tempfile.TemporaryDirectory() as tmp, patch.object(
                port, "DSI_SHA256", hashlib.sha256(original).hexdigest()):
            root = Path(tmp)
            path = root / port.DSI_PATH
            path.parent.mkdir(parents=True)
            path.write_bytes(original)
            port.stage_dsi(root)
            expected = path.read_bytes()
            port.stage_dsi(root)
            self.assertEqual(path.read_bytes(), expected)
            for addition in port.DSI_INSERTIONS.values():
                expected = expected.replace(addition.encode(), b"")
            self.assertEqual(expected, original)
            path.write_bytes(path.read_bytes() + b"unknown edit\n")
            before = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "baseline"):
                port.stage_dsi(root)
            self.assertEqual(path.read_bytes(), before)
            if hasattr(Path, "symlink_to") and shutil.which("cc"):
                path.unlink()
                victim = root / "victim"
                victim.write_bytes(original)
                try:
                    path.symlink_to(victim)
                except OSError as error:
                    if getattr(error, "winerror", None) == 1314:
                        self.skipTest("Windows symlink privilege required; covered in WSL")
                    raise
                with self.assertRaisesRegex(ValueError, "symlink"):
                    port.stage_dsi(root)
                self.assertEqual(victim.read_bytes(), original)

    def test_dsi_diagnostics_require_unique_fixed_source_anchors(self):
        for original in (b"", ("\n".join(port.DSI_INSERTIONS) * 2).encode()):
            with patch.object(port, "DSI_SHA256", hashlib.sha256(original).hexdigest()):
                with self.assertRaisesRegex(ValueError, "anchors"):
                    port.transform_dsi(original)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_abl_ubwc_identity_gate_and_disabled_profile(self):
        harness = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;
#define IS_ENABLED(option) (option)
struct device_node { const char *compatible[4], *model; int nc, nm, nb, ns; u32 board[2], msm[2]; };
static int of_property_count_strings(struct device_node *n, const char *p) { return !strcmp(p,"model") ? n->nm : n->nc; }
static int of_device_is_compatible(struct device_node *n, const char *s) {
 for(int i=0;i<n->nc;i++) if(!strcmp(n->compatible[i],s)) return 1;
 return 0;
}
static int of_property_read_string(struct device_node *n, const char *p, const char **s) { *s=n->model; return !*s; }
static int of_property_count_u32_elems(struct device_node *n, const char *p) { return !strcmp(p,"qcom,board-id") ? n->nb : n->ns; }
static int of_property_read_u32_array(struct device_node *n, const char *p, u32 *s, int count) {
 memcpy(s,!strcmp(p,"qcom,board-id") ? n->board : n->msm,count*sizeof(*s)); return 0;
}
#include "umi_abl_ubwc.h"
int main(void) {
#if IS_ENABLED(CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT)
 struct device_node base={{"qcom,kona-mtp","qcom,kona","qcom,mtp"},"Qualcomm Technologies, Inc. xiaomi umi",3,1,2,2,{0x2b,0},{356,0x20001}};
 assert(miseed_umi_abl_ubwc_root(&base));
 for(int i=0;i<10;i++) {
  struct device_node n=base;
  switch(i) {
   case 0: n.board[0]=0x24; break;
   case 1: n.board[1]=1; break;
   case 2: n.msm[0]=355; break;
   case 3: n.nb=3; break;
   case 4: n.ns=1; break;
   case 5: n.compatible[0]="qcom,kona-hdk"; break;
   case 6: n.nc=2; break;
   case 7: n.model="Qualcomm Technologies, Inc. xiaomi cmi"; break;
   case 8: n.nm=2; break;
   case 9: n.model=0; break;
  }
  assert(!miseed_umi_abl_ubwc_root(&n));
 }
#endif
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binding = root / "dt-bindings/arm/qcom,ids.h"
            binding.parent.mkdir(parents=True)
            binding.write_text("#define QCOM_ID_SM8250 356\n")
            source = root / "ubwc.c"
            source.write_text(harness)
            for enabled in (0, 1):
                binary = root / ("test" + str(enabled))
                result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-unused-function", "-I", str(root),
                    "-I", str(ROOT / "kernel/display"),
                    "-DCONFIG_DRM_PANEL_XIAOMI_UMI_CSOT=" + str(enabled),
                    str(source), "-o", str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(binary)], check=True)

    def test_ubwc_staging_is_idempotent_and_refuses_unknown_source(self):
        original = b"#include <linux/soc/qcom/ubwc.h>\n" + port.UBWC_CALL.encode()
        with tempfile.TemporaryDirectory() as tmp, patch.object(
                port, "UBWC_SHA256", hashlib.sha256(original).hexdigest()):
            source = Path(tmp)
            path = source / port.UBWC_PATH
            path.parent.mkdir(parents=True)
            path.write_bytes(original)
            port.stage_ubwc(source, ROOT / "kernel/display")
            expected = path.read_bytes()
            port.stage_ubwc(source, ROOT / "kernel/display")
            self.assertEqual(path.read_bytes(), expected)
            self.assertEqual(expected.replace(b'#include "umi_abl_ubwc.h"\n', b"").replace(
                port.UBWC_QUIRK.encode(), b""), original)
            path.write_bytes(expected + b"unknown modification\n")
            with self.assertRaisesRegex(ValueError, "baseline"):
                port.stage_ubwc(source, ROOT / "kernel/display")
            self.assertEqual(path.read_bytes(), expected + b"unknown modification\n")

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_commands_fail_closed_reset_and_zero_brightness(self):
        source = (ROOT / "kernel/display/panel-xiaomi-umi-csot.c").read_text()
        first = source.index("struct umi_csot {")
        last = source.index("static const struct drm_display_mode")
        body = source[first:last]
        for name in ("umi_csot_prepare", "umi_csot_unprepare", "umi_csot_brightness"):
            start = source.index("static int " + name + "(")
            body += source[start:source.index("\n}\n", start) + 3]
        harness = (ROOT / "tests/native_panel_harness.c").read_text().replace("/* PANEL_UNDER_TEST */", body)
        with tempfile.TemporaryDirectory() as tmp:
            src, binary = Path(tmp) / "panel.c", Path(tmp) / "panel"
            src.write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-I", str(ROOT / "kernel/display"), str(src), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_fixed_mode_and_profile_boundaries(self):
        source = (ROOT / "kernel/display/panel-xiaomi-umi-csot.c").read_text()
        self.assertEqual(round(1112 * 3532 * 60 / 1000), 235655)
        self.assertNotIn("DEVICE_ATTR", source)
        self.assertNotIn("regulator_set_voltage", source)
        self.assertIn("GPIOD_ASIS", source)
        self.assertNotIn("CONFIG_DRM_MSM=y", (ROOT / "kernel/umi-l2.config").read_text())
        table = (ROOT / "kernel/display/umi_csot_commands.h").read_text()
        self.assertEqual(len(re.findall(r"\{ \d+, \d+, \{", table)), 59)
