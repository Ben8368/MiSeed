"""Exercise the real rescue probe's empty-protocol guard without NFC hardware."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HARNESS = r'''
#include "rescue_nfc_l3.c"
#include <assert.h>
static int negative_rc, first_rc, starts, stops, downs, ups;
static int discover, found;
unsigned int sleep(unsigned int seconds) { assert(seconds == 12 && discover); return 0; }
int nfc_nl_open(struct nfc_nl_client *c) { return 0; }
void nfc_nl_close(struct nfc_nl_client *c) { }
int nfc_nl_dump(struct nfc_nl_client *c, uint8_t cmd, const unsigned char *a,
                size_t len, nfc_nl_visit_fn visit, void *ctx) {
    if (cmd == NFC_CMD_GET_TARGET) {
        assert(discover && starts == 1); struct nfc_target_info *t = ctx;
        t->count = found; t->protocol = NFC_PROTO_MIFARE_MASK; return 0;
    }
    assert(cmd == NFC_CMD_GET_DEVICE);
    struct nfc_device_info *dev = ctx;
    dev->count = 1; dev->protocols = NFC_PROTO_ISO14443_MASK | NFC_PROTO_MIFARE_MASK; return 0;
}
int nfc_nl_attr_u32(unsigned char *a, size_t cap, size_t *len, uint16_t type, uint32_t value) {
    assert(*len+sizeof(value) <= cap); memcpy(a+*len,&value,sizeof(value)); *len+=sizeof(value); return 0;
}
int nfc_nl_ack(struct nfc_nl_client *c, uint8_t cmd, const unsigned char *a, size_t len) {
    switch (cmd) {
    case NFC_CMD_DEV_UP: ups++; return first_rc;
    case NFC_CMD_START_POLL: {
        uint32_t mask; assert(len == 8); memcpy(&mask,a+4,4); starts++;
        if (discover) { assert(mask == NFC_PROTO_MIFARE_MASK); return 0; }
        if (starts == 1) { assert(!mask); return negative_rc; }
        assert(negative_rc == -EINVAL && mask); return -ENODEV;
    }
    case NFC_CMD_DEV_DOWN: downs++; return 0;
    case NFC_CMD_STOP_POLL: stops++; return 0;
    default: abort();
    }
}
int nfc_nl_attr_read(const unsigned char *a,size_t n,uint16_t t,void *v,size_t l) { abort(); }
int nfc_nl_attr_string(const unsigned char *a,size_t n,uint16_t t,char *v,size_t l) { abort(); }
int read_type4_fixture(uint32_t d,uint32_t t,uint32_t p) { abort(); }
int main(int argc,char **argv) {
    if (argc==4) {
        discover=1; first_rc=atoi(argv[2]); found=atoi(argv[3]);
        run_nfc_mifare_discovery(); run_nfc_mifare_discovery();
        assert(ups==1 && starts==!first_rc && stops==!first_rc && downs==!first_rc);
        return 0;
    }
    if (argc==3) {
        first_rc=atoi(argv[2]); run_nfc_first(); run_nfc_first();
        assert(run_nfc_l3_probe(0)==-EALREADY && ups==1 && !starts && !stops && !downs);
        return 0;
    }
    assert(argc==2); negative_rc=atoi(argv[1]); int rc=run_nfc_l3_probe(1);
    assert(ups==1 && downs==1 && stops==(negative_rc==0));
    assert(starts==(negative_rc==-EINVAL ? 2 : 1));
    assert(rc==(negative_rc==-EINVAL ? -ENODEV : negative_rc ? negative_rc : -EUCLEAN));
    run_nfc_first(); assert(ups==1); return 0;
}
'''


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('gcc'), 'Linux NFC headers/GCC required')
class ProbeStopTests(unittest.TestCase):
    def test_only_einval_permits_real_poll(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'stop'
            headers = Path(__file__).resolve().parents[1] / 'kernel'
            subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-I',
                            str(headers), '-x', 'c', '-', '-o', str(exe)], input=HARNESS, text=True, check=True)
            for rc in (-22, -110, -1, 0):
                with self.subTest(rc=rc):
                    subprocess.run([exe, str(rc)], capture_output=True, check=True, timeout=3)
            for rc in (0, -110, -6):
                with self.subTest(first_rc=rc):
                    output = subprocess.check_output([exe, 'first', str(rc)], text=True, timeout=3)
                    self.assertIn(f'NFC_FIRST_RESULT stage=dev_up rc={rc}\n', output)
                    self.assertIn('NFC_FIRST_RESULT stage=guard rc=-114\n', output)
            for rc, count in ((-110, 0), (0, 0), (0, 1), (0, 2)):
                result = subprocess.check_output([exe, 'discovery', str(rc), str(count)], text=True, timeout=3)
                self.assertIn(f'rc={rc or (0 if count==1 else -61)} cleanup=0\n', result.split('NFC_DISCOVERY_END')[0])
                self.assertIn('stage=guard up=0 polling=0', result)
