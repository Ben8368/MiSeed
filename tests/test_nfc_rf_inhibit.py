"""Exercise the NCI RF inhibit transition with isolated kernel API doubles."""

# SPDX-License-Identifier: AGPL-3.0-only

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
typedef unsigned int u32;
#define NCI_IDLE 0
#define NCI_DISCOVERY 1
#define NCI_POLL_ACTIVE 2
#define NCI_W4_ALL_DISCOVERIES 3
#define pr_info(...) ((void)0)
#define NFC_RF_TARGET 2
#define NCI_DEACTIVATE_TYPE_IDLE_MODE 0
#define NCI_RF_DEACTIVATE_TIMEOUT 100
#define WRITE_ONCE(x, v) ((x) = (v))
#define EXPORT_SYMBOL_GPL(x)
#define msecs_to_jiffies(x) (x)
#define atomic_read(x) (*(x))
struct device { bool locked; };
struct target { u32 idx; };
struct nfc_dev {
    struct device dev;
    bool dev_up;
    bool polling;
    int rf_mode;
    struct target *active_target;
};
struct nci_dev {
    struct nfc_dev *nfc_dev;
    bool rf_inhibited;
    bool rf_deactivate_pending;
    int state;
    int target_active_prot;
};
static int requests, request_error, target_lost, polls_aborted, listen_aborted;
static bool idle_response = true;
static void device_lock(struct device *dev) { assert(!dev->locked); dev->locked = true; }
static void device_unlock(struct device *dev) { assert(dev->locked); dev->locked = false; }
static void nci_rf_deactivate_req(struct nci_dev *dev, const void *type) {
    assert(dev->rf_inhibited && type == (void *)NCI_DEACTIVATE_TYPE_IDLE_MODE);
}
static int nci_request(struct nci_dev *dev, void (*req)(struct nci_dev *, const void *),
                       const void *type, int timeout) {
    assert(!dev->nfc_dev->dev.locked && timeout == NCI_RF_DEACTIVATE_TIMEOUT);
    requests++;
    req(dev, type);
    if (idle_response)
        dev->state = NCI_IDLE;
    return request_error;
}
static void nfc_target_lost(struct nfc_dev *dev, u32 idx) {
    assert(!dev->dev.locked && dev->active_target->idx == idx);
    target_lost++;
    dev->active_target = NULL;
}
static void nfc_tm_deactivated(struct nfc_dev *dev) {
    assert(!dev->dev.locked && dev->rf_mode == NFC_RF_TARGET);
    listen_aborted++;
    dev->rf_mode = 0;
}
static void nfc_targets_found(struct nfc_dev *dev, const void *targets, int count) {
    assert(!dev->dev.locked && !targets && !count);
    if (dev->polling) {
        polls_aborted++;
        dev->polling = false;
    }
}
#include "nci-rf-inhibit.inc"

int main(void) {
    struct nfc_dev nfc = {};
    struct nci_dev nci = { .nfc_dev = &nfc };
    struct target tag = { .idx = 7 };

    assert(!nci_set_rf_inhibited(&nci, true) && nci.rf_inhibited && !requests);
    assert(!nci_set_rf_inhibited(&nci, false) && !nci.rf_inhibited && !nfc.dev_up);
    nfc.dev_up = nfc.polling = true;
    nci.state = NCI_DISCOVERY;
    request_error = -ETIMEDOUT;
    assert(nci_set_rf_inhibited(&nci, true) == -ETIMEDOUT);
    assert(nci.rf_inhibited && nci.rf_deactivate_pending && nfc.polling);
    assert(nci.state == NCI_IDLE && requests == 1 && !polls_aborted);
    assert(nci_set_rf_inhibited(&nci, false) == -ETIMEDOUT && nci.rf_inhibited);
    request_error = 0;
    assert(!nci_set_rf_inhibited(&nci, false));
    assert(!nci.rf_inhibited && !nci.rf_deactivate_pending && requests == 3);
    assert(nfc.dev_up && !nfc.polling && polls_aborted == 1);
    nci.state = NCI_POLL_ACTIVE;
    nci.target_active_prot = 1;
    nfc.active_target = &tag;
    idle_response = false;
    assert(nci_set_rf_inhibited(&nci, true) == -EIO);
    assert(nci.rf_inhibited && nfc.active_target == &tag && !target_lost);
    idle_response = true;
    assert(!nci_set_rf_inhibited(&nci, true));
    assert(nci.rf_inhibited && !nfc.active_target && !nci.target_active_prot);
    assert(target_lost == 1 && requests == 5);
    nfc.rf_mode = NFC_RF_TARGET;
    assert(!nci_set_rf_inhibited(&nci, true) && listen_aborted == 1);
    assert(!nci_set_rf_inhibited(&nci, false) && nfc.dev_up && !nci.rf_inhibited);
}
"""


class NciRfInhibitTest(unittest.TestCase):
    def test_gate_and_confirmed_idle_before_unblock(self):
        compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "rf.c"
            binary = Path(directory) / ("rf.exe" if os.name == "nt" else "rf")
            source.write_text(HARNESS, encoding="utf-8")
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I",
                            ROOT / "kernel/nfc", source, "-o", binary], check=True)
            subprocess.run([binary], check=True)
