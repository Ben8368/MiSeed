"""Standard UAPI, chip independence and Umi registration regression tests."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tools.source_layout import POWER_RULES

ROOT = Path(__file__).resolve().parents[1]


class WiredInterfaceTests(unittest.TestCase):
    def test_chip_boundary_and_no_private_userspace_control_abi(self):
        source = "\n".join(p.read_text() for p in (ROOT / "kernel/power").glob("*.c"))
        for forbidden in ("DEVICE_ATTR", "charge_test", "fastcharge_test"):
            self.assertNotIn(forbidden, source)
        rules = "\n".join(POWER_RULES)
        self.assertNotIn("+= umi-bq25970.o", rules)
        self.assertNotIn("+= bq25970_core.o", rules)
        self.assertIn("qcom_pm8150b_charger-$(CONFIG_CHARGER_MISEED_UMI_PPS)", rules)
        self.assertIn("obj-$(CONFIG_CHARGER_BQ25970_CORE) += bq25970_charger.o", rules)
        policy = "\n".join(p.read_text() for p in (ROOT / "kernel/power").glob("umi_pps*.c"))
        for forbidden in ("regmap", "BQ_ENABLE", "bq25970_update(", "bq25970_verify("):
            self.assertNotIn(forbidden, policy)
        driver = (ROOT / "kernel/power/umi_pps_driver.c").read_text()
        self.assertIn('"qcom_pm8150b_charger.charger"', driver)
        console = (ROOT / "kernel/rescue_init.c").read_text()
        self.assertNotIn("charge-test", console)
        self.assertIn("charge_behaviour", console)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_usb_type_tracks_pd_and_bc12_without_hiding_read_errors(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_supply.c").read_text()
        start = source.index("static int smb5_get_usb_type(")
        function = source[start:source.index("\n}\n", start) + 3]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
enum { POWER_SUPPLY_USB_TYPE_UNKNOWN, POWER_SUPPLY_USB_TYPE_SDP,
 POWER_SUPPLY_USB_TYPE_CDP, POWER_SUPPLY_USB_TYPE_DCP, POWER_SUPPLY_USB_TYPE_C,
 POWER_SUPPLY_USB_TYPE_PD, POWER_SUPPLY_USB_TYPE_PD_PPS, POWER_SUPPLY_USB_TYPE_PD_DRP,
 POWER_SUPPLY_PROP_USB_TYPE };
union power_supply_propval {int intval;};
struct smb5_chip {void *pd;};
static int online, online_error, pd_error, pd_type, apsd_error, apsd_type;
static int apsd_reads, pd_reads;
static int smb5_get_prop_usb_online(struct smb5_chip *c, int *v)
{*v=online;return online_error;}
static int power_supply_get_property(void *pd, int prop, union power_supply_propval *v)
{assert(pd && prop==POWER_SUPPLY_PROP_USB_TYPE);pd_reads++;v->intval=pd_type;return pd_error;}
static int smb5_apsd_get_charger_type(struct smb5_chip *c, int *v)
{apsd_reads++;*v=apsd_type;return apsd_error;}
''' + function + r'''
int main(void) {
 struct smb5_chip chip={.pd=&chip};int type=-1;
 /* Detach must ignore stale TCPM capability and APSD state. */
 pd_type=POWER_SUPPLY_USB_TYPE_PD_PPS;
 assert(!smb5_get_usb_type(&chip,&type) && type==POWER_SUPPLY_USB_TYPE_UNKNOWN);
 assert(!pd_reads && !apsd_reads);
 online_error=-EIO;assert(smb5_get_usb_type(&chip,&type)==-EIO);
 assert(!pd_reads && !apsd_reads);online_error=0;online=1;
 for(int t=POWER_SUPPLY_USB_TYPE_PD;t<=POWER_SUPPLY_USB_TYPE_PD_DRP;t++) {
  pd_type=t;apsd_error=-EAGAIN;
  assert(!smb5_get_usb_type(&chip,&type) && type==t && !apsd_reads);
 }
 pd_error=-EIO;assert(smb5_get_usb_type(&chip,&type)==-EIO && !apsd_reads);
 pd_error=-EAGAIN;assert(smb5_get_usb_type(&chip,&type)==-EAGAIN && !apsd_reads);
 pd_error=0;pd_type=POWER_SUPPLY_USB_TYPE_C;apsd_error=0;
 for(int t=POWER_SUPPLY_USB_TYPE_SDP;t<=POWER_SUPPLY_USB_TYPE_DCP;t++) {
  apsd_type=t;assert(!smb5_get_usb_type(&chip,&type) && type==t);
 }
 apsd_error=-EAGAIN;assert(smb5_get_usb_type(&chip,&type)==-EAGAIN);
 apsd_error=-EIO;assert(smb5_get_usb_type(&chip,&type)==-EIO);
 chip.pd=NULL;apsd_error=0;apsd_type=POWER_SUPPLY_USB_TYPE_DCP;
 int before=pd_reads;
 assert(!smb5_get_usb_type(&chip,&type) && type==apsd_type && pd_reads==before);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "usb_type.c", Path(tmp) / "usb_type"
            src.write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", str(src), "-o", str(exe)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

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
static struct i2c_driver umi_bq25970_driver;
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
