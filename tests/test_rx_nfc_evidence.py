"""Exercise the retained-log acceptance checks and detach state machine."""

# SPDX-License-Identifier: AGPL-3.0-only

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.rx_nfc_evidence import assess, ExportError


ROOT = Path(__file__).resolve().parents[1]


def fixture(mode="poll", *, idle="discovery", usb="configured", partner="0", charge=0, result=0):
    events = [
        f"RX_NFC stage=armed mode={mode}", "RX_NFC stage=detached online=0",
        "RX_NFC stage=seated online=1", f"USB udc=a600000.usb state={usb}",
        f"TYPEC port=port0 partner={partner} rc=0",
        "SAMPLE supply=rx1619 online=1 voltage_now=5317000 current_now=87000",
        "SAMPLE supply=qcom-battery status=Discharging current_now=-140000",
        f"KERNEL miseed-power chg_enable={charge} usb_rt=0x10 power_path=0x11",
        f"KERNEL miseed-rx-nfc rf_idle previous={idle}",
        "RX_NFC stage=blocked start_poll=ERFKILL", "RX_NFC stage=removed online=0",
        "RX_NFC stage=rediscovered targets=1 mifare=1",
        f"RX_NFC stage=result mode={mode} at=rediscover rc={result} cleanup=0",
    ]
    return (f"CHARGE_LOG_BEGIN boot_id=synthetic first=0 next={len(events)} count={len(events)} storage_errors=0 ram_only=1\n" +
            "".join(f"seq={i} boot_ms={i * 100} {event}\n" for i, event in enumerate(events)) +
            f"CHARGE_LOG_END next={len(events)}\n").encode("ascii")


HARNESS = r'''
#define open fake_open
#define read fake_read
#define close fake_close
#define sleep fake_sleep
#define usleep fake_usleep
#include "rescue_rx_nfc.c"
#include <assert.h>

static int phase, scenario, starts;
int umi_typec_partner(void) { return scenario == 1 && phase >= 2; }
int fake_open(const char *path, int flags, ...) {
    (void)flags;
    if (!strcmp(path, RX_SUPPLY "online")) return 2;
    assert(!strcmp(path, "fake-udc")); return 1;
}
ssize_t fake_read(int fd, void *out, size_t size) {
    const char *s;
    if (fd == 1) s = scenario == 1 && phase >= 2 ? "configured\n" : "not attached\n";
    else s = phase == 1 || phase == 2 ? "1\n" : "0\n";
    assert(strlen(s) < size); memcpy(out, s, strlen(s)); return strlen(s);
}
int fake_close(int fd) { (void)fd; return 0; }
unsigned int fake_sleep(unsigned int n) { return n; }
int fake_usleep(useconds_t n) { assert(n == 250000); return 0; }
int nfc_poll(struct nfc_nl_client *client, uint32_t index, uint32_t protocols) {
    (void)client; assert(index == 7 && protocols == MIFARE);
    starts++;
    if (phase == 2) return scenario == 2 ? 0 : starts == 1 ? -EBUSY : -ERFKILL;
    assert(phase == 4); return starts == 3 ? -ERFKILL : 0;
}
int nfc_get_targets(struct nfc_nl_client *c, uint32_t i, struct nfc_target_info *tag) {
    (void)c; assert(i == 7 && phase == 4);
    tag->count = 1; tag->protocol = MIFARE; return 0;
}
void charge_log_append(const char *kind, const char *format, ...) {
    const char *expected[] = {"stage=detached online=0", "stage=seated online=1",
        "stage=blocked start_poll=ERFKILL", "stage=removed online=0",
        "stage=rediscovered targets=1 mifare=1"};
    assert(!strcmp(kind, "RX_NFC") && phase < 5 && !strcmp(format, expected[phase]));
    phase++;
}
int main(void) {
    struct nfc_nl_client c = {};
    const char *stage;
    assert(!observe_cycle(&c, 7, "fake-udc", &stage) && phase == 5 && starts == 4);
    phase = starts = 0; scenario = 1;
    assert(observe_cycle(&c, 7, "fake-udc", &stage) == -ENOLINK && phase == 2 && !starts);
    phase = starts = 0; scenario = 2;
    assert(observe_cycle(&c, 7, "fake-udc", &stage) == -EUCLEAN && phase == 2 && starts == 1);
}
'''


class RxNfcEvidenceTests(unittest.TestCase):
    def test_valid_poll_and_target_with_signed_battery_sample(self):
        for mode, idle in (("poll", "discovery"), ("target", "target")):
            report = assess(fixture(mode, idle=idle), mode)
            self.assertEqual(report["pmic_power_path"],
                             [{"raw": 0x11, "input_valid": True, "usb_in": True, "dc_in": False}])
            self.assertEqual(report["battery"], [{"current_ua": -140000, "status": "Discharging"}])

    def test_rejects_false_passes(self):
        for raw in (fixture(idle="target"), fixture(partner="1"), fixture(partner="unknown"),
                    fixture(charge=1), fixture(result=-110),
                    fixture().replace(b"seq=4 ", b"seq=3 "),
                    fixture().replace(b"voltage_now=5317000", b"voltage_now=unavailable"),
                    fixture().split(b"CHARGE_LOG_END")[0]):
            with self.subTest(raw=raw), self.assertRaises(ExportError):
                assess(raw, "poll")

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "Linux GCC required")
    def test_observe_cycle_stops_on_early_usb_or_unexpected_rf(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "cycle"
            subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-ffunction-sections",
                            "-Wl,--gc-sections", "-I", ROOT / "kernel", "-x", "c", "-", "-o", exe],
                           input=HARNESS, text=True, check=True)
            subprocess.run([exe], check=True, timeout=5)
