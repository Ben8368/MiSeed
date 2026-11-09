"""Pinned PD integration and actual RX error paths; no hardware access."""
# SPDX-License-Identifier: AGPL-3.0-only
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import apply_pd_diagnostics as applier

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
struct device { int unused; };
struct pd_message { uint16_t header; uint32_t payload[7]; } __attribute__((packed));
struct pmic_typec_pdphy { struct device *dev; void *regmap,*tcpm_port; int base,lock; };
#define USB_PDPHY_RX_SIZE_REG 1
#define USB_PDPHY_RX_STATUS_REG 2
#define USB_PDPHY_RX_BUFFER_REG 3
#define USB_PDPHY_RX_ACKNOWLEDGE_REG 4
#define TCPC_TX_SOP 0
#define spin_lock_irqsave(lock,flags) do { (flags)=0; } while(0)
#define spin_unlock_irqrestore(lock,flags) ((void)(flags))
static unsigned int wire_size;
static int fail_at, calls, forwarded, reads, ack, warnings, zero_header;
static struct pd_message delivered;
static int io_fail(void) { return ++calls == fail_at ? -EIO : 0; }
static int regmap_read(void *m,int reg,unsigned int *v) {
 int ret=io_fail(); if(ret)return ret; *v=reg==1?wire_size:0x20;return 0;
}
static int regmap_bulk_read(void *m,int reg,u8 *data,int size) {
 int ret=io_fail();if(ret)return ret;reads++;
 memset(data,0x5a,size);data[0]=zero_header?0:3;
 data[1]=zero_header?0:((size-2)/4)<<4;return 0;
}
static int regmap_write(void *m,int reg,int value) {
 ack++;assert(reg==4 && value==0);return io_fail();
}
static void tcpm_pd_receive(void *p,struct pd_message *msg,int type) {
 assert(type==TCPC_TX_SOP);forwarded++;delivered=*msg;
}
static void dev_warn_ratelimited(struct device *d,const char *fmt,...) {
 char text[256];va_list args;va_start(args,fmt);vsnprintf(text,sizeof(text),fmt,args);va_end(args);
 assert(strstr(text,"miseed-pdphy"));warnings++;
}
@RECEIVE@
static void reset(unsigned int size) {
 wire_size=size;fail_at=calls=forwarded=reads=ack=warnings=zero_header=0;
 memset(&delivered,0xff,sizeof(delivered));
}
int main(void) {
 struct pmic_typec_pdphy phy={0};
 for(unsigned int size=0;size<256;size++) {
  reset(size);qcom_pmic_typec_pdphy_pd_receive(&phy);
  if(size<1 || size>29) {
   assert(!forwarded && !reads && ack==1 && warnings>=1);
  } else {
   /* 1+4*N encodes valid header/object lengths; other in-range lengths
    * deliberately exercise malformed transport input left to TCPM validation.
    */
   assert(forwarded==1 && reads==1 && ack==1 && !warnings);
   /* Bytes outside the actual wire message must not expose stack data. */
   for(unsigned int i=size+1;i<sizeof(delivered);i++)assert(((u8 *)&delivered)[i]==0);
  }
 }
 for(int failure=1;failure<=4;failure++) {
  reset(5);fail_at=failure;qcom_pmic_typec_pdphy_pd_receive(&phy);
  assert(!forwarded && warnings>=1);
  assert(ack==1);
 }
 reset(0);fail_at=2;qcom_pmic_typec_pdphy_pd_receive(&phy);
 assert(!forwarded && !reads && ack==1 && warnings>=1);
 reset(1);zero_header=1;qcom_pmic_typec_pdphy_pd_receive(&phy);
 assert(forwarded==1 && ack==1 && warnings==1 && delivered.header==0);
 puts("RX invalid sizes, I/O errors, ACK failures, zero header and tail initialization passed");
 return 0;
}
'''


class PdDiagnosticsTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_actual_receive_rejects_unread_frames_and_preserves_valid_path(self):
        receive=(ROOT / "kernel/power/pmic_pdphy_receive.inc").read_text()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);entry=root / "receive.c";binary=root / "receive"
            entry.write_text(HARNESS.replace("@RECEIVE@",receive))
            result=subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                "-Wno-unused-parameter",str(entry),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)

    def fixtures(self, root):
        (root / ".miseed-source-revision").write_text(applier.REVISION)
        originals={
            applier.PDPHY: 'static void qcom_pmic_typec_pdphy_pd_receive(void) {}\nstatic irqreturn_t stub;\ncase PMIC_PDPHY_SIG_RX_IRQ:\n\t\tschedule_work();\n\tdev_vdbg(dev, "pd_transmit_signal: type %d negotiate_rev %d send %d\\n",\n type, rev, ret);\n',
            applier.TCPM: "#define LOG_BUFFER_ENTRIES\t1024\n" + "\n".join(old for old, new in applier.TCPM_EDITS),
        }
        for name,text in originals.items():
            path=root / name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,newline="\n")
        return {name:hashlib.sha256(text.encode()).hexdigest() for name,text in originals.items()}

    def test_staging_is_hash_locked_idempotent_and_detects_modified_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);hashes=self.fixtures(root)
            with patch.object(applier,"ORIGINAL",hashes):
                first=applier.stage(root);second=applier.stage(root)
                self.assertEqual(first,second)
                self.assertIn("262144",(root/applier.TCPM).read_text())
                self.assertIn("kvcalloc",(root/applier.TCPM).read_text())
                self.assertNotIn("*logbuffer[",(root/applier.TCPM).read_text())
                self.assertIn("miseed-pdphy rx_irq_signal",(root/applier.PDPHY).read_text())
                (root/applier.PDPHY).write_text("changed")
                with self.assertRaisesRegex(ValueError,"patched PD source changed"):applier.stage(root)

    def test_wrong_revision_or_later_bad_hash_changes_no_source_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);hashes=self.fixtures(root)
            before=(root/applier.PDPHY).read_bytes()
            with patch.object(applier,"ORIGINAL",hashes):
                (root / ".miseed-source-revision").write_text("wrong")
                with self.assertRaisesRegex(ValueError,"revision"):applier.stage(root)
                (root / ".miseed-source-revision").write_text(applier.REVISION)
                (root/applier.TCPM).write_text("changed")
                with self.assertRaisesRegex(ValueError,"SHA-256"):applier.stage(root)
                self.assertEqual((root/applier.PDPHY).read_bytes(),before)
                self.assertFalse((root/".miseed-pd-diagnostics.json").exists())

    def test_changed_manifest_or_receive_input_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);hashes=self.fixtures(root)
            with patch.object(applier,"ORIGINAL",hashes):
                applier.stage(root)
                changed=root/"changed.inc";changed.write_text("changed")
                with self.assertRaisesRegex(ValueError,"inputs changed"):applier.stage(root,changed)


if __name__ == "__main__":
    unittest.main()
