"""Standard UAPI and single-module charger registration regression tests."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tools.source_layout import POWER_RULES

ROOT = Path(__file__).resolve().parents[1]


class WiredInterfaceTests(unittest.TestCase):
    def test_no_private_control_abi_or_cross_module_charger_exports(self):
        source = "\n".join(p.read_text() for p in (ROOT / "kernel/power").glob("*.c"))
        for forbidden in ("EXPORT_SYMBOL", "DEVICE_ATTR", "charge_test", "fastcharge_test"):
            self.assertNotIn(forbidden, source)
        rules = "\n".join(POWER_RULES)
        self.assertNotIn("+= umi-bq25970.o", rules)
        self.assertNotIn("+= bq25970_core.o", rules)
        self.assertIn("qcom_pm8150b_charger-$(CONFIG_CHARGER_MISEED_UMI_PPS)", rules)
        self.assertIn("qcom_pm8150b_charger-$(CONFIG_CHARGER_BQ25970_CORE)", rules)
        driver = (ROOT / "kernel/power/umi_pps_driver.c").read_text()
        self.assertIn('"qcom_pm8150b_charger.charger"', driver)
        console = (ROOT / "kernel/rescue_init.c").read_text()
        self.assertNotIn("charge-test", console)
        self.assertIn("charge_behaviour", console)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_real_registration_rollback_and_reverse_teardown(self):
        source = (ROOT / "kernel/power/umi_wired_driver.c").read_text()
        source = "\n".join(line for line in source.splitlines()
                           if not line.startswith(("#include", "MODULE_", "module_", "extern ")))
        harness = r'''
#include <assert.h>
#include <errno.h>
#define __init
#define __exit
#define IS_ENABLED(x) (x)
struct platform_driver {int unused;};
struct auxiliary_driver {int unused;};
struct i2c_driver {int unused;};
static struct platform_driver qcom_spmi_smb5;
static struct auxiliary_driver umi_pps_driver;
static struct i2c_driver bq25970_driver;
static int fail, sequence[8], n;
static int platform_driver_register(void *d) {sequence[n++]=1;return fail==1?-EIO:0;}
static int auxiliary_driver_register(void *d) {sequence[n++]=2;return fail==2?-EIO:0;}
static int i2c_add_driver(void *d) {sequence[n++]=3;return fail==3?-EIO:0;}
static void platform_driver_unregister(void *d) {sequence[n++]=-1;}
static void auxiliary_driver_unregister(void *d) {sequence[n++]=-2;}
static void i2c_del_driver(void *d) {sequence[n++]=-3;}
''' + source + r'''
int main(void) {
 assert(!umi_wired_init());
 umi_wired_exit();
#if CONFIG_CHARGER_MISEED_UMI_PPS
 assert(n==6 && sequence[0]==1 && sequence[1]==2 && sequence[2]==3);
 assert(sequence[3]==-2 && sequence[4]==-3 && sequence[5]==-1);
 n=0;fail=2;assert(umi_wired_init()==-EIO);
 assert(n==3 && sequence[0]==1 && sequence[1]==2 && sequence[2]==-1);
 n=0;fail=3;assert(umi_wired_init()==-EIO);
 assert(n==5 && sequence[3]==-2 && sequence[4]==-1);
#elif CONFIG_CHARGER_BQ25970_CORE
 assert(n==4 && sequence[1]==3 && sequence[2]==-3 && sequence[3]==-1);
 n=0;fail=3;assert(umi_wired_init()==-EIO);
 assert(n==3 && sequence[2]==-1);
#else
 assert(n==2 && sequence[0]==1 && sequence[1]==-1);
#endif
 n=0;fail=1;assert(umi_wired_init()==-EIO && n==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "registration.c", Path(tmp) / "registration"
            src.write_text(harness)
            for pump, core in ((0, 0), (0, 1), (1, 1)):
                result = subprocess.run(["cc", "-std=c11", "-Wall", "-Werror",
                    "-Wno-unused-function", "-Wno-unused-variable",
                    f"-DCONFIG_CHARGER_MISEED_UMI_PPS={pump}",
                    f"-DCONFIG_CHARGER_BQ25970_CORE={core}", str(src), "-o", str(exe)],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(exe)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
