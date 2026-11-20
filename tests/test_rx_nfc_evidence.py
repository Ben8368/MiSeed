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


def fixture(mode="poll", *, idle="discovery", usb="configured", partner="0", charge=0,
            result=0, battery="Discharging", delayed=False, loss=False, cc=0, watermark=True,
            seated_vbus=0, initial_vbus=0, final_vbus=0):
    events = [
        f"RX_NFC stage=armed mode={mode}", "RX_NFC stage=detached online=0",
        "RX_NFC stage=seated online=1", f"USB udc=a600000.usb state={usb}",
        f"TYPEC port=port0 partner={partner} rc=0",
        "SAMPLE supply=rx1619 online=1 voltage_now=5317000 current_now=87000",
        f"SAMPLE supply=qcom-battery status={battery} current_now=-140000",
        f"KERNEL 6,27,600000,-;smb5: miseed-power chg_enable={charge} usb_rt=0x10 power_path=0x11",
        f"KERNEL 6,28,700000,-;smb5: miseed-rx-diag smb5_off=1 cc_attached={cc} "
        f"vbus_present={seated_vbus} cc_status=0x{(seated_vbus << 5) | cc:02x}",
        f"KERNEL 6,29,800000,-;miseed-rx-nfc rf_idle previous={idle}",
        "RX_NFC stage=blocked start_poll=ERFKILL", "RX_NFC stage=removed online=0",
        "RX_NFC stage=rediscovered targets=1 mifare=1",
        f"RX_NFC stage=result mode={mode} at=rediscover rc={result} cleanup=0",
        "KERNEL 6,26,175000,-;rx1619 16-0061: power-good changed, pg=1  SUBSYSTEM=i2c",
        "KERNEL 6,30,1080000,-;rx1619 16-0061: power-good changed, pg=0  SUBSYSTEM=i2c",
        "KERNEL 6,25,90000,-;smb5: miseed-rx-diag smb5_off=1 cc_attached=0 "
        f"vbus_present={initial_vbus} cc_status=0x{initial_vbus << 5:02x}",
        "KERNEL 6,31,1200000,-;smb5: miseed-rx-diag smb5_off=1 cc_attached=0 "
        f"vbus_present={final_vbus} cc_status=0x{final_vbus << 5:02x}",
    ]
    if delayed:
        events.insert(14, events[9])
        events[9] = "USB udc=a600000.usb state=configured"
    if watermark:
        events.append("KERNEL 6,32,1900000,-;smb5: miseed-rx-diag smb5_off=1 cc_attached=0 vbus_present=0 cc_status=0x00")
    if loss:
        events.append("LOSS kernel_ring_overrun")
    return (f"CHARGE_LOG_BEGIN boot_id=synthetic first=0 next={len(events)} count={len(events)} storage_errors=0 ram_only=1\n" +
            "".join(f"seq={i} boot_ms={i * 100 if i < 14 else 1900 + i * 100} {event}\n"
                    for i, event in enumerate(events)) +
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
static int detach_checks, removal_checks, cc_checks;
int umi_typec_partner(void) { return scenario == 1 && phase >= 2; }
int umi_typec_detach_evidence(int allow_shared_vbus) {
    if (phase == 0) {
        assert(!allow_shared_vbus);
        if (scenario == 3 && !detach_checks++) return -EAGAIN;
        if (scenario == 4 && !cc_checks++) return -ENOLINK;
    }
    if (phase == 1 || phase == 2) assert(allow_shared_vbus);
    if (phase == 2 && scenario == 5) return -ENOLINK;
    if (phase == 3 && !allow_shared_vbus && !removal_checks++) return -EAGAIN;
    if (phase == 4) assert(!allow_shared_vbus);
    return 0;
}
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
    assert(!observe_cycle(&c, 7, "fake-udc", &stage) && phase == 5 && starts == 4 && removal_checks == 2);
    phase = starts = 0; scenario = 1;
    assert(observe_cycle(&c, 7, "fake-udc", &stage) == -ENOLINK && phase == 2 && !starts);
    phase = starts = 0; scenario = 2;
    assert(observe_cycle(&c, 7, "fake-udc", &stage) == -EUCLEAN && phase == 2 && starts == 1);
    phase = starts = 0; scenario = 3;
    assert(!observe_cycle(&c, 7, "fake-udc", &stage) && phase == 5 && detach_checks > 1);
    phase = starts = 0; scenario = 4;
    assert(!observe_cycle(&c, 7, "fake-udc", &stage) && phase == 5 && cc_checks == 2);
    phase = starts = 0; scenario = 5;
    assert(observe_cycle(&c, 7, "fake-udc", &stage) == -ENOLINK && phase == 2 && !starts);
}
'''


# Compile the real recorder predicate with a deterministic recent printk sample.
RECORDER_SHIM = r"""
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
struct charge_log_header { uint64_t total; };
struct charge_log_record { char text[240]; };
static int charge_log_fd;
static const char *record;
static uint64_t charge_boot_ms(void) { return 3000; }
static int charge_log_lock(short type) { (void)type; return 0; }
static int charge_log_io(void *data, size_t len, size_t offset, int writing) {
    assert(!writing);
    if (!offset) {
        assert(len == sizeof(struct charge_log_header));
        ((struct charge_log_header *)data)->total = 1;
    } else {
        assert(len == sizeof(struct charge_log_record));
        snprintf(((struct charge_log_record *)data)->text, len, "%s", record);
    }
    return 0;
}
"""
RECORDER_CASES = r"""
#define DIAG "seq=0 boot_ms=3000 KERNEL 6,27,2900000,-;smb5: miseed-rx-diag "
#define OFF_CC "smb5_off=1 cc_attached=0 "
int main(void) {
    record = DIAG OFF_CC "vbus_present=0 cc_status=0x02";
    assert(!umi_typec_detach_evidence(0));
    record = DIAG OFF_CC "vbus_present=1 cc_status=0x22";
    assert(umi_typec_detach_evidence(0) == -EAGAIN && !umi_typec_detach_evidence(1));
    record = DIAG "smb5_off=1 cc_attached=1 vbus_present=1 cc_status=0x2b";
    assert(umi_typec_detach_evidence(1) == -ENOLINK);
    record = DIAG "smb5_off=0 cc_attached=0 vbus_present=1 cc_status=0x22";
    assert(umi_typec_detach_evidence(1) == -EIO);
    record = DIAG "smb5_off=1 cc_error=-5";
    assert(umi_typec_detach_evidence(1) == -EIO);
    record = DIAG OFF_CC "vbus_present=1 cc_status=0x02";
    assert(umi_typec_detach_evidence(1) == -EIO);
    record = "seq=0 boot_ms=3000 KERNEL 6,27,999000,-;smb5: miseed-rx-diag "
             OFF_CC "vbus_present=1 cc_status=0x22";
    assert(umi_typec_detach_evidence(1) == -EAGAIN);
    record = "seq=0 boot_ms=3000 LOSS kernel_ring_overrun";
    assert(umi_typec_detach_evidence(1) == -EAGAIN);
}
"""


class RxNfcEvidenceTests(unittest.TestCase):
    def test_valid_poll_and_target_with_signed_battery_sample(self):
        for mode, idle in (("poll", "discovery"), ("target", "target")):
            report = assess(fixture(mode, idle=idle), mode)
            self.assertEqual(report["pmic_power_path"],
                             [{"raw": 0x11, "input_valid": True, "usb_in": True, "dc_in": False}])
            self.assertEqual(report["battery"], [{"current_ua": -140000, "status": "Discharging"}])
            self.assertEqual((report["rx_pg_on_us"], report["rf_idle_us"], report["rx_pg_off_us"]),
                             (175000, 800000, 1080000))

    def test_delayed_kernel_drain_uses_event_time_and_preserves_status(self):
        report = assess(fixture(delayed=True, battery="Not charging"), "poll")
        self.assertEqual(report["battery"], [{"current_ua": -140000, "status": "Not charging"}])
        with self.assertRaisesRegex(ExportError, "idle confirmation"):
            assess(fixture(delayed=True).replace(b"29,800000", b"29,2500000"), "poll")

    def test_rejects_idle_outside_the_confirmed_rx_window(self):
        for mode, idle in (("poll", "discovery"), ("target", "target")):
            frame = fixture(mode, idle=idle, delayed=True)
            for timestamp_us in (150000, 1090000, 1100000, 1150000):
                with self.subTest(mode=mode, timestamp_us=timestamp_us), self.assertRaisesRegex(
                        ExportError, "idle confirmation while RX was online"):
                    assess(frame.replace(b",29,800000,", f",29,{timestamp_us},".encode()), mode)
            for replacement in (b"other event", b"rx1619 16-0061: power-good changed, pg=-5"):
                with self.assertRaisesRegex(ExportError, "power-good rise and fall"):
                    assess(frame.replace(b"rx1619 16-0061: power-good changed, pg=1", replacement), mode)

    def test_vbus_from_shared_path_does_not_override_cc_or_partner(self):
        self.assertTrue(assess(fixture(seated_vbus=1), "poll")["vbus_detected_while_seated"])
        self.assertFalse(assess(fixture(), "poll")["vbus_detected_while_seated"])
        for frame, message in (
                (fixture(seated_vbus=1, initial_vbus=1), "unplug before seating"),
                (fixture(seated_vbus=1, final_vbus=1), "VBUS detected after"),
                (fixture(seated_vbus=1, cc=1), "Type-C CC/VBUS"),
                (fixture(seated_vbus=1, partner="1"), "Type-C partner"),
                (fixture(seated_vbus=1).replace(b"cc_status=0x20", b"cc_status=0x00"), "Type-C CC/VBUS"),
                (fixture().replace(b"25,90000", b"25,1000000"), "unplug before seating"),
                (fixture().replace(b"31,1200000", b"31,1050000"), "VBUS clear after RX removal")):
            with self.subTest(message=message), self.assertRaisesRegex(ExportError, message):
                assess(frame, "poll")

    def test_rejects_false_passes(self):
        for raw in (fixture(idle="target"), fixture(partner="1"), fixture(partner="unknown"),
                    fixture(charge=1), fixture(result=-110), fixture(loss=True), fixture(cc=1),
                    fixture(watermark=False),
                    fixture().replace(b"seq=4 ", b"seq=3 "),
                    fixture().replace(b"voltage_now=5317000", b"voltage_now=unavailable"),
                    fixture().split(b"CHARGE_LOG_END")[0]):
            with self.subTest(raw=raw), self.assertRaises(ExportError):
                assess(raw, "poll")

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "Linux GCC required")
    def test_recorder_typec_predicate_checks_raw_status_and_freshness(self):
        recorder = (ROOT / "kernel/charge_log.c").read_text(encoding="utf-8")
        predicate = recorder[recorder.index("int umi_typec_detach_evidence("):
                             recorder.index("void charge_snapshot(")]
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "predicate"
            subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-x", "c", "-", "-o", exe],
                           input=RECORDER_SHIM + predicate + RECORDER_CASES, text=True, check=True)
            subprocess.run([exe], check=True, timeout=5)

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("gcc"), "Linux GCC required")
    def test_observe_cycle_stops_on_early_usb_or_unexpected_rf(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "cycle"
            subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-ffunction-sections",
                            "-Wl,--gc-sections", "-I", ROOT / "kernel", "-x", "c", "-", "-o", exe],
                           input=HARNESS, text=True, check=True)
            subprocess.run([exe], check=True, timeout=5)
