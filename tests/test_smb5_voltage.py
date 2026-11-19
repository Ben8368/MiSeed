"""Replay PM8150B's loaded-MID/unloaded-USBIN voltage selection and errors."""
# SPDX-License-Identifier: AGPL-3.0-only
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc"), "C compiler required")
class Smb5VoltageTests(unittest.TestCase):
    def test_voltage_uses_physical_mid_under_load_and_fails_without_estimation(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_hw.c").read_text()
        start = source.index("int smb5_get_input_voltage(")
        function = source[start:source.index("\n}\n", start) + 3]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#define USBIN_CMD_IL 0x340
#define USBIN_SUSPEND_BIT 1
struct smb5_chip { void *regmap, *usb_in_v_chan, *mid_chg_chan; unsigned int base; };
static int online=1, online_error, read_error, adc_error;
static unsigned int command;
static int mid_uv=8580000, usb_uv=9100000, mid_reads, usb_reads;
static int smb5_get_prop_usb_online(struct smb5_chip *c,int *value)
{ (void)c; *value=online; return online_error; }
static int regmap_read(void *map,unsigned int reg,unsigned int *value)
{ (void)map; assert(reg==0x1340); *value=command; return read_error; }
static int smb5_get_iio_chan(struct smb5_chip *c,void *chan,int *value) {
 if(chan==c->mid_chg_chan) { mid_reads++; *value=mid_uv; }
 else { assert(chan==c->usb_in_v_chan);usb_reads++;*value=usb_uv; }
 return adc_error;
}
''' + function + r'''
int main(void) {
 int mid,usb,v=0;
 struct smb5_chip c={.base=0x1000,.mid_chg_chan=&mid,.usb_in_v_chan=&usb};
 /* An anomalously high USBIN reading must not drive the loaded policy. */
 assert(!smb5_get_input_voltage(&c,&v) && v==8580000 && !usb_reads);
 command=1;assert(!smb5_get_input_voltage(&c,&v) && v==9100000 && usb_reads==1);
 command=0;mid_uv=900000;
 assert(smb5_get_input_voltage(&c,&v)==-ENODATA && usb_reads==1);
 mid_uv=1000000;assert(!smb5_get_input_voltage(&c,&v) && v==1000000);
 for(int suspended=0;suspended<2;suspended++) {
  command=suspended;adc_error=-EIO;
  assert(smb5_get_input_voltage(&c,&v)==-EIO);
 }
 adc_error=0;read_error=-EIO;int count=mid_reads+usb_reads;
 assert(smb5_get_input_voltage(&c,&v)==-EIO && count==mid_reads+usb_reads);
 read_error=0;online_error=-EIO;
 assert(smb5_get_input_voltage(&c,&v)==-EIO && count==mid_reads+usb_reads);
 online_error=0;online=0;
 assert(!smb5_get_input_voltage(&c,&v) && v==0 && count==mid_reads+usb_reads);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path, binary = Path(directory) / "voltage.c", Path(directory) / "voltage"
            path.write_text(code)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     str(path), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
