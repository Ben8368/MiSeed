"""Exercise MDSS takeover reset errors and the compiled DT reset identity."""
# SPDX-License-Identifier: GPL-2.0-only
# msm_mdss_reset fixture: Copyright (c) 2018, The Linux Foundation.

import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import apply_display_port as port
from tools import isolate_abl_targets as isolation


# Function from the pinned Linux msm_mdss.c, with only its comments omitted.
RESET = '''static int msm_mdss_reset(struct device *dev)
{
	struct reset_control *reset;

	reset = reset_control_get_optional_exclusive(dev, NULL);
	if (!reset) {
		return 0;
	} else if (IS_ERR(reset)) {
		return dev_err_probe(dev, PTR_ERR(reset),
				     "failed to acquire mdss reset\\n");
	}

	reset_control_assert(reset);
	msleep(20);
	reset_control_deassert(reset);

	reset_control_put(reset);

	return 0;
}
'''


class MdssResetTests(unittest.TestCase):
    def test_stage_is_exact_idempotent_and_rejects_unknown_edits(self):
        original = RESET.encode()
        with tempfile.TemporaryDirectory() as tmp, patch.object(
                port, "MDSS_SHA256", hashlib.sha256(original).hexdigest()):
            root = Path(tmp)
            path = root / port.MDSS_PATH
            path.parent.mkdir(parents=True)
            path.write_bytes(original)
            port.stage_mdss(root)
            expected = path.read_bytes()
            self.assertNotEqual(original, expected)
            port.stage_mdss(root)
            self.assertEqual(path.read_bytes(), expected)
            path.write_bytes(expected.replace(b"msleep(20)", b"msleep(1)"))
            before = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "baseline"):
                port.stage_mdss(root)
            self.assertEqual(path.read_bytes(), before)
            if shutil.which("cc"):
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
                    port.stage_mdss(root)
                self.assertEqual(victim.read_bytes(), original)

    def test_rejects_missing_or_duplicated_reset_anchors(self):
        for original in (b"", (RESET * 2).encode()):
            with patch.object(port, "MDSS_SHA256", hashlib.sha256(original).hexdigest()):
                with self.assertRaisesRegex(ValueError, "anchors"):
                    port.transform_mdss(original)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_reset_order_and_failure_cleanup(self):
        harness = r'''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
struct device { int unused; };
struct reset_control { int unused; };
static struct reset_control handle;
static int scenario;
static char trace[16];
static void mark(char c) { size_t n = strlen(trace); trace[n] = c; trace[n+1] = 0; }
#define IS_ERR(p) ((intptr_t)(p) < 0)
#define PTR_ERR(p) ((int)(intptr_t)(p))
static struct reset_control *reset_control_get_optional_exclusive(struct device *d, void *id) {
 mark('g'); return scenario == 0 ? NULL : scenario == 1 ? (void *)(intptr_t)-517 : &handle;
}
static int dev_err_probe(struct device *d, int ret, const char *msg) { mark('e'); return ret; }
static int reset_control_assert(struct reset_control *r) { mark('a'); return scenario == 2 ? -5 : 0; }
static int reset_control_deassert(struct reset_control *r) { mark('d'); return scenario == 3 ? -22 : 0; }
static void reset_control_put(struct reset_control *r) { mark('p'); }
static void msleep(int ms) { assert(ms == 20); mark('s'); }
/* RESET_UNDER_TEST */
int main(void) {
 struct device dev = {0};
 int results[] = {0, -517, -5, -22, 0};
 const char *traces[] = {"g", "ge", "gaep", "gasdep", "gasdp"};
 for (scenario = 0; scenario < 5; scenario++) {
  trace[0] = 0;
  assert(msm_mdss_reset(&dev) == results[scenario]);
  assert(!strcmp(trace, traces[scenario]));
 }
 return 0;
}
'''
        with patch.object(port, "MDSS_SHA256", hashlib.sha256(RESET.encode()).hexdigest()):
            body = port.transform_mdss(RESET.encode()).decode()
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / "reset.c", Path(tmp) / "reset"
            source.write_text(harness.replace("/* RESET_UNDER_TEST */", body))
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", str(source), "-o", str(binary)],
                           check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, timeout=10)


@unittest.skipUnless(all(shutil.which(t) for t in ("dtc", "fdtget", "fdtput")),
                     "libfdt tools required")
class NativeResetDtTests(unittest.TestCase):
    def test_reset_must_be_core_of_the_display_controller(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, dtb = root / "board.dts", root / "board.dtb"
            source.write_text('''/dts-v1/;
/ {
 dispcc: clock-controller { compatible = "qcom,sm8250-dispcc"; #reset-cells = <1>; };
 other: other-reset { #reset-cells = <1>; };
 mdss: display-subsystem { compatible = "qcom,sm8250-mdss"; resets = <&dispcc 0>; };
 consumer { resets = <&other 0>; };
};''')
            subprocess.run(["dtc", "-q", "-@", "-I", "dts", "-O", "dtb",
                            "-o", str(dtb), str(source)], check=True, capture_output=True)
            isolation.verify_native_reset(dtb)
            for reset in ("<&dispcc 1>", "<&other 0>", "<&dispcc 0>, <&dispcc 1>"):
                text = source.read_text().replace("resets = <&dispcc 0>", "resets = " + reset)
                bad = root / "bad.dts"
                bad.write_text(text)
                subprocess.run(["dtc", "-q", "-@", "-I", "dts", "-O", "dtb",
                                "-o", str(dtb), str(bad)], check=True, capture_output=True)
                with self.assertRaisesRegex(ValueError, "MDSS_CORE_BCR"):
                    isolation.verify_native_reset(dtb)
            subprocess.run(["fdtput", "-d", str(dtb), "/display-subsystem", "resets"], check=True)
            with self.assertRaises(subprocess.CalledProcessError):
                isolation.verify_native_reset(dtb)
