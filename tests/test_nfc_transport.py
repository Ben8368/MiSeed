"""Exercise production SN100T C functions with isolated kernel API doubles."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHIM = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Linux transport errno absent from Darwin libc. */
#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif
typedef uint8_t u8;
#define U8_MAX 255
#define NCI_CTRL_HDR_SIZE 3
#define GFP_KERNEL 0
#define IRQ_HANDLED 1
typedef int irqreturn_t;
struct mutex { int held; };
struct device { void *phy; };
struct i2c_client { int irq; struct device dev; };
struct nci_dev { void *data; };
struct gpio_desc { int value; int failure; int writes; };
struct sk_buff { unsigned int len; u8 *data; };
static int held, depth=1, syncs, sends, received, live, oom;
static int send_result=999, read_result[2], read_count;
static u8 packet[258];
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held=1; held++; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held=0; held--; }
static void enable_irq(int irq) { (void)irq; assert(depth==1); depth--; }
static void disable_irq_nosync(int irq) { (void)irq; assert(depth==0); depth++; }
static void synchronize_irq(int irq) { (void)irq; assert(!held); syncs++; }
#define dev_err_ratelimited(...) ((void)0)
#define dev_dbg(...) ((void)0)
static void dev_info_stub(struct device *d, const char *fmt, ...) {
 (void)d; (void)fmt;
}
#define dev_info(...) dev_info_stub(__VA_ARGS__)
static void usleep_range(int a,int b) { (void)a;(void)b; }
static void *nci_get_drvdata(struct nci_dev *n) { return n->data; }
static int gpiod_get_value_cansleep(struct gpio_desc *g) { return g->value; }
static int gpiod_direction_output(struct gpio_desc *g,int value) {
 g->writes++; if(g->failure) return g->failure; g->value=value; return 0;
}
static struct sk_buff *alloc_skb(int length,int flags) {
 (void)flags; assert(length==258); if(oom) return NULL;
 struct sk_buff *s=calloc(1,sizeof(*s)); s->data=calloc(1,length); live++; return s;
}
static void *skb_put(struct sk_buff *s,int n) {
 assert(s->len+n<=258); void *p=s->data+s->len; s->len+=n; return p;
}
static void kfree_skb(struct sk_buff *s) { assert(s); free(s->data);free(s);live--; }
static int i2c_master_recv(struct i2c_client *c,void *dst,int n) {
 (void)c; assert(read_count<2); int i=read_count++;
 int ret=read_result[i]; if(ret>0) {assert(ret<=n);memcpy(dst,packet+(i?3:0),ret);} return ret;
}
static int i2c_master_send(struct i2c_client *c,const void *src,int n) {
 (void)c;(void)src;sends++;return send_result==999?n:send_result;
}
static int nci_recv_frame(struct nci_dev *n,struct sk_buff *s) {
 (void)n;received++;kfree_skb(s);return 0;
}
static struct i2c_client *to_i2c_client(struct device *d) { return (void *)((char *)d - offsetof(struct i2c_client,dev)); }
static void *i2c_get_clientdata(struct i2c_client *c) { return c->dev.phy; }
"""
MAIN = r"""
static struct sk_buff *tx(unsigned int length) {
 struct sk_buff *s=alloc_skb(258,0); skb_put(s,length); if(length>=3)s->data[2]=length-3;return s;
}
int main(int argc,char **argv) {
 assert(argc==2); int test=atoi(argv[1]);
 struct i2c_client client={.irq=5};struct nci_dev ndev={0};
 struct gpio_desc ven={.value=1}, firm={0};
 struct sn100x p={.client=&client,.ndev=&ndev,.enable=&ven,.firmware=&firm};
 ndev.data=&p;client.dev.phy=&p;
 struct sk_buff *s=NULL;
 if(test==0) { /* Exact header, zero and maximum payload; no phantom body read. */
  read_result[0]=3;assert(sn100x_read(&p,&s)==0 && s->len==3 && read_count==1);kfree_skb(s);
  read_count=0;packet[2]=255;read_result[1]=255;
  assert(sn100x_read(&p,&s)==0 && s->len==258 && read_count==2);kfree_skb(s);
 } else if(test==1) { /* Short/negative header and body errors release the skb. */
  for(int i=-1;i<3;i++){ read_count=0;read_result[0]=i;
   assert(sn100x_read(&p,&s)==(i<0?i:-EBADMSG));assert(!s && !live); }
  for(int i=-1;i<4;i++){read_count=0;packet[2]=4;read_result[0]=3;read_result[1]=i;
   assert(sn100x_read(&p,&s)==(i<0?i:-EBADMSG));assert(!s && !live);}
  oom=1;assert(sn100x_read(&p,&s)==-ENOMEM && !s);
 } else if(test==2) { /* Normalize inherited FIRM high without cycling shared VEN. */
  firm.value=1;assert(sn100x_open(&ndev)==0);
  assert(firm.value==0 && ven.value==1 && depth==0 && ven.writes==1);
  sn100x_close(&ndev);assert(p.fault==-ETIMEDOUT && ven.value==1 && depth==1);
  p.fault=0;firm.failure=-EIO;assert(sn100x_open(&ndev)==-EIO);assert(ven.writes==1);
  firm.failure=0;ven.failure=-EIO;assert(sn100x_open(&ndev)==-EIO);assert(!p.opened && depth==1);
 } else if(test==3) { /* Valid/invalid TX, partial transfer latched with balanced IRQ. */
  assert(sn100x_send(&ndev,tx(3))==-ENETDOWN && !sends);
  assert(sn100x_open(&ndev)==0);assert(sn100x_send(&ndev,tx(258))==0);
  assert(sn100x_send(&ndev,tx(2))==-EINVAL && sends==1);
  s=tx(4);s->data[2]=0;assert(sn100x_send(&ndev,s)==-EINVAL && sends==1);
  send_result=2;assert(sn100x_send(&ndev,tx(3))==-EREMOTEIO);assert(depth==1 && p.fault);
  assert(sn100x_send(&ndev,tx(3))==-EREMOTEIO && sends==2);
  sn100x_close(&ndev);sn100x_close(&ndev);assert(depth==1 && syncs==2 && ven.value==1);
  assert(sn100x_open(&ndev)==-EREMOTEIO);
 } else if(test==4) { /* Level IRQ read failure disables once, no retry storm. */
  assert(sn100x_open(&ndev)==0);read_result[0]=-ENXIO;
  sn100x_irq(5,&p);sn100x_irq(5,&p);assert(read_count==1 && depth==1 && !received);
  sn100x_close(&ndev);assert(ven.value==1 && firm.value==0 && p.fault==-ENXIO);
 } else if(test==5) { /* NCI 2.x requires successful response + solicited notification. */
  assert(sn100x_open(&ndev)==0);s=tx(4);s->data[0]=0x20;assert(!sn100x_send(&ndev,s));
  s=tx(4);s->data[0]=0x40;sn100x_reset_response(&p,s);kfree_skb(s);
  assert(p.reset_acked && !p.reset_confirmed);
  s=tx(12);s->data[0]=0x60;s->data[3]=1;s->data[5]=0x20;s->data[7]=4;
  sn100x_reset_response(&p,s);assert(!p.reset_confirmed);s->data[3]=2;
  sn100x_reset_response(&p,s);assert(p.reset_confirmed);kfree_skb(s);
  sn100x_close(&ndev);assert(!p.fault && depth==1 && ven.value==1);
  assert(!sn100x_suspend(&client.dev));assert(sn100x_open(&ndev)==-EBUSY);
  assert(!sn100x_resume(&client.dev));assert(!sn100x_open(&ndev));
 } else if(test==6) { /* Failed close/reset timeout cannot silently permit suspend. */
  assert(!sn100x_open(&ndev));assert(sn100x_suspend(&client.dev)==-EBUSY);
  sn100x_close(&ndev);assert(p.fault==-ETIMEDOUT && ven.value==1);
  assert(sn100x_suspend(&client.dev)==-EBUSY);assert(sn100x_open(&ndev)==-ETIMEDOUT);
 } else if(test==7) { /* NCI 1.x completion and incoming delivery. */
  assert(!sn100x_open(&ndev));p.reset_pending=true;
  packet[0]=0x40;packet[2]=3;packet[4]=0x10;read_result[0]=3;read_result[1]=3;
  sn100x_irq(5,&p);assert(received==1 && p.reset_confirmed);
  sn100x_close(&ndev);assert(!p.fault && depth==1);
 } else if(test==8) {
  assert(!sn100x_open(&ndev));send_result=-ENXIO;
  assert(sn100x_send(&ndev,tx(3))==-ENXIO && depth==1);assert(p.fault==-ENXIO);
 } else if(test==9) {
  assert(!sn100x_open(&ndev));oom=1;sn100x_irq(5,&p);
  assert(p.fault==-ENOMEM && !read_count && depth==1);
 } else {assert(0);}
 assert(!live && !held && ven.value==1);return 0;
}
"""


@unittest.skipUnless(shutil.which('cc') or shutil.which('gcc'), 'C compiler required')
class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        text = (ROOT / 'kernel/nfc/sn100x.c').read_text(encoding='utf-8')
        logic = text[text.index('#define SN100X_FRAME_MAX'):text.index('static const struct nci_ops')]
        pm = text[text.index('static int sn100x_suspend'):text.index('static DEFINE_SIMPLE_DEV_PM_OPS')]
        path = Path(cls.temp.name) / 'transport.c'
        path.write_text('#include <stddef.h>\n' + SHIM + logic + pm + MAIN, encoding='utf-8')
        cls.binary = Path(cls.temp.name) / 'transport'
        result = subprocess.run(
            [shutil.which('cc') or shutil.which('gcc'), '-std=c11', '-Wall',
             '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
             str(path), '-o', str(cls.binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def test_production_transport_failure_and_lifecycle_paths(self):
        for case in range(10):
            with self.subTest(case=case):
                subprocess.run([self.binary, str(case)], check=True, capture_output=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
