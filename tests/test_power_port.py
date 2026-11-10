"""Offline safety boundaries for the unqualified FG/SMB5 compile port."""

# SPDX-License-Identifier: AGPL-3.0-only

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import apply_power_port


ROOT = Path(__file__).resolve().parents[1]


class PowerPortTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_smb5_sink_current_converts_adc_voltage_and_rejects_boost(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        start = source.index("static int smb5_get_input_current(")
        function = source[start:source.index("\n}\n", start) + 3]
        constants = "\n".join(line for line in source.splitlines()
                              if line.startswith("#define SMB5_"))
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#define BIT(n) (1U << (n))
struct smb5_chip { void *regmap, *usb_in_i_chan; unsigned int base; };
static int online = 1, adc_uv = 480000, adc_error, otg_error, reads;
static unsigned int otg;
static int smb5_get_prop_usb_online(struct smb5_chip *chip, int *value)
{ (void)chip; *value = online; return 0; }
static int regmap_read(void *map, unsigned int reg, unsigned int *value)
{ (void)map; assert(reg == 0x1140); reads++; *value = otg; return otg_error; }
static int smb5_get_iio_chan(struct smb5_chip *chip, void *channel, int *value)
{ (void)chip; (void)channel; reads++; *value = adc_uv; return adc_error; }
''' + constants + "\n" + function + r'''
int main(void) {
 struct smb5_chip chip = { .base = 0x1000 };
 int value = 123;
 assert(!smb5_get_input_current(&chip, &value) && value == 2400000);
 online = 0; reads = 0;
 assert(!smb5_get_input_current(&chip, &value) && value == 0 && !reads);
 online = 1; value = 123; otg = 1; reads = 0;
 assert(smb5_get_input_current(&chip, &value) == -ENODATA && value == 123 && reads == 1);
 otg = 0; otg_error = -EIO;
 assert(smb5_get_input_current(&chip, &value) == -EIO && value == 123);
 otg_error = 0; adc_error = -EIO;
 assert(smb5_get_input_current(&chip, &value) == -EIO && value == 123);
 adc_error = 0; adc_uv = -1;
 assert(smb5_get_input_current(&chip, &value) == -ERANGE && value == 123);
 adc_uv = INT_MAX;
 assert(smb5_get_input_current(&chip, &value) == -ERANGE && value == 123);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            entry, binary = Path(directory) / "input.c", Path(directory) / "input"
            entry.write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     str(entry), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_fg_standard_current_sign_and_read_errors(self):
        source = (ROOT / "kernel/power/qcom_fg.c").read_text()
        start = source.index("static int qcom_fg_current_now(")
        function = source[start:source.index("\n}\n", start) + 3]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
struct qcom_fg_chip;
struct fg_ops { int (*get_current)(struct qcom_fg_chip *, int *); };
struct qcom_fg_chip { const struct fg_ops *ops; };
static int raw_current, read_error;
static int read_current(struct qcom_fg_chip *chip, int *value)
{ (void)chip; if (read_error) return read_error; *value = raw_current; return 0; }
''' + function + r'''
int main(void) {
 const struct fg_ops ops = { .get_current = read_current };
 struct qcom_fg_chip chip = { .ops = &ops };
 int value = 123;
 raw_current = -2400000;
 assert(!qcom_fg_current_now(&chip, &value) && value == 2400000);
 raw_current = 386000;
 assert(!qcom_fg_current_now(&chip, &value) && value == -386000);
 raw_current = 0;
 assert(!qcom_fg_current_now(&chip, &value) && value == 0);
 raw_current = INT_MAX;
 assert(!qcom_fg_current_now(&chip, &value) && value == -INT_MAX);
 value = 123; raw_current = INT_MIN;
 assert(qcom_fg_current_now(&chip, &value) == -ERANGE && value == 123);
 read_error = -EIO;
 assert(qcom_fg_current_now(&chip, &value) == -EIO && value == 123);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            entry, binary = Path(directory) / "current.c", Path(directory) / "current"
            entry.write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     str(entry), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=10)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        (self.source / ".miseed-source-revision").write_text(apply_power_port.REVISION)
        self.target = self.source / "drivers/power/supply"
        self.target.mkdir(parents=True)
        for name in ("Makefile", "Kconfig"):
            (self.target / name).write_text("# fixture\n")
        self.rpmhpd_kconfig = self.source / "drivers/pmdomain/qcom/Kconfig"
        self.rpmhpd_kconfig.parent.mkdir(parents=True)
        self.rpmhpd_kconfig.write_text(
            'config QCOM_RPMHPD\n'
            '\ttristate "Qualcomm RPMh Power domain driver"\n'
            '\tdepends on QCOM_RPMH && QCOM_COMMAND_DB\n'
        )

    def stage(self):
        apply_power_port.stage(self.source, ROOT / "kernel/power")

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_standard_input_limit_and_behaviour_failure_paths(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        functions = []
        for name in ("smb5_set_current_limit", "smb5_set_property", "smb5_property_is_writable"):
            start = source.rindex("static int " + name + "(")
            functions.append(source[start:source.index("\n}\n", start) + 3])
        harness = (ROOT / "tests/smb5_abi_harness.c").read_text().replace(
            "/* MISEED_SMB5_ABI_UNDER_TEST */", "\n".join(functions))
        with tempfile.TemporaryDirectory() as directory:
            src, binary = Path(directory) / "abi.c", Path(directory) / "abi"
            src.write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Werror",
                                     "-Wno-unused-function", str(src), "-o", str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_restage_replaces_stale_generated_driver_without_duplicate_rules(self):
        self.stage()
        snapshot = {p.name: p.read_bytes() for p in self.target.iterdir()}
        rpmhpd_snapshot = self.rpmhpd_kconfig.read_bytes()
        self.assertIn(b"\tselect PM_OPP\n", rpmhpd_snapshot)
        (self.target / "qcom_fg.c").write_text("stale source\n")
        self.stage()
        self.assertEqual(snapshot, {p.name: p.read_bytes() for p in self.target.iterdir()})
        self.assertEqual(rpmhpd_snapshot, self.rpmhpd_kconfig.read_bytes())

    def test_wrong_revision_does_not_mutate_tree(self):
        (self.source / ".miseed-source-revision").write_text("0" * 40)
        with self.assertRaisesRegex(ValueError, "pinned Linux"):
            self.stage()
        self.assertFalse((self.target / "qcom_fg.c").exists())

    @unittest.skipIf(os.name == "nt", "POSIX FIFO fixture")
    def test_nonregular_revision_marker_is_rejected_before_read(self):
        marker = self.source / ".miseed-source-revision"
        marker.unlink()
        os.mkfifo(marker)
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.stage()

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlinked_power_destination_directory_is_rejected(self):
        external = self.source / "external"
        external.mkdir()
        (external / "Makefile").write_text("keep\n")
        shutil.rmtree(self.target)
        self.target.symlink_to(external, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink directory"):
            self.stage()
        self.assertEqual((external / "Makefile").read_text(), "keep\n")

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlinked_board_destination_directory_is_rejected(self):
        dts_root = self.source / "arch/arm64/boot/dts"
        dts_root.mkdir(parents=True)
        external = self.source / "external-board"
        external.mkdir()
        (dts_root / "qcom").symlink_to(external, target_is_directory=True)
        with self.assertRaises(SystemExit):
            with patch("sys.argv", ["apply_power_port", "--source", str(self.source)]):
                apply_power_port.main()
        self.assertEqual(list(external.iterdir()), [])

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlink_does_not_overwrite_external_file(self):
        external = self.source / "external.c"
        external.write_text("preserve\n")
        (self.target / "qcom_fg.c").symlink_to(external)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.stage()
        self.assertEqual(external.read_text(), "preserve\n")

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "POSIX Bash required")
    def test_build_rejects_modular_or_missing_runtime_power_driver(self):
        script = (ROOT / "tools/build_kernel_l2.sh").read_text()
        start = script.index("for option in BATTERY_MISEED_QCOM_FG")
        stop = script.index("for option in MEMFD_CREATE", start)
        check = 'set -eu\nbuild_tree="$1"\nfail() { exit 42; }\n' + script[start:stop]
        for state, expected in (("y", 0), ("m", 42), ("n", 42)):
            with self.subTest(state=state):
                (self.source / ".config").write_text(
                    f"CONFIG_BATTERY_MISEED_QCOM_FG={state}\n"
                    f"CONFIG_CHARGER_MISEED_PM8150B={state}\n"
                    "CONFIG_MODULES=y\nCONFIG_POWER_SUPPLY=y\nCONFIG_IIO=y\nCONFIG_QCOM_SPMI_ADC5=y\n"
                    "CONFIG_DRM=y\nCONFIG_DRM_SIMPLEDRM=y\nCONFIG_DRM_FBDEV_EMULATION=y\n"
                    "CONFIG_DRM_CLIENT_DEFAULT_FBDEV=y\nCONFIG_FB_CORE=y\nCONFIG_FRAMEBUFFER_CONSOLE=y\n"
                    "CONFIG_GPIOLIB=y\nCONFIG_NET=y\nCONFIG_NFC=y\nCONFIG_NFC_NCI=y\nCONFIG_NFC_SN100X=y\n"
                    "CONFIG_CHARGER_MISEED_UMI_PPS=y\nCONFIG_I2C_QCOM_GENI=y\n"
                    "CONFIG_TYPEC_TCPM=y\nCONFIG_TYPEC_QCOM_PMIC=y\n"
                    "CONFIG_REGULATOR_QCOM_USB_VBUS=y\nCONFIG_USB=y\n")
                result = subprocess.run(["bash", "-c", check, "fixture", str(self.source)])
                self.assertEqual(result.returncode, expected)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_standard_behaviour_enum_cannot_reverse_enable_or_accept_force_discharge(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        functions = []
        for name in ("smb5_get_prop_charging_enabled", "smb5_set_prop_charging_enabled"):
            start = source.index("static int " + name + "(")
            stop = source.index("\n}\n", start) + 3
            functions.append(source[start:stop])
        harness = r'''
#include <assert.h>
#include <errno.h>
#define CHARGING_ENABLE_CMD 0x42
#define CHARGING_ENABLE_CMD_BIT 1
enum { POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO = 0,
       POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE = 1,
       POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE = 2 };
struct smb5_chip { void *regmap; void *dev; unsigned int base; };
static unsigned int written, reads, writes, command;
static int failure;
#define dev_err(...) ((void)0)
static int regmap_read(void *map, unsigned int reg, unsigned int *v)
{ (void)map; assert(reg == 0x1042); ++reads; *v = command; return failure; }
static int regmap_update_bits(void *map, unsigned int reg,
                              unsigned int mask, unsigned int v)
{ (void)map; assert(reg == 0x1042); assert(mask == 1); ++writes; written = v; return failure; }
'''
        harness += "\n".join(functions) + r'''
int main(void) {
    struct smb5_chip chip = { .base = 0x1000 };
    int result = -1;
    assert(!smb5_set_prop_charging_enabled(&chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO));
    assert(writes == 1 && written == 1);
    assert(!smb5_set_prop_charging_enabled(&chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE));
    assert(writes == 2 && written == 0);
    assert(smb5_set_prop_charging_enabled(&chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE) == -EINVAL);
    assert(smb5_set_prop_charging_enabled(&chip, (unsigned int)-1) == -EINVAL);
    assert(writes == 2);
    command = 0x81;
    assert(!smb5_get_prop_charging_enabled(&chip, &result) && result == 0);
    command = 0x80;
    assert(!smb5_get_prop_charging_enabled(&chip, &result) && result == 1);
    failure = -EIO;
    result = 77;
    assert(smb5_get_prop_charging_enabled(&chip, &result) == -EIO && result == 77);
    assert(smb5_set_prop_charging_enabled(&chip, 0) == -EIO);
    assert(reads == 3);
    return 0;
}
'''
        entry = self.source / "behaviour.c"
        binary = self.source / "behaviour"
        entry.write_text(harness)
        result = subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(entry), "-o", str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_fg_capacity_endpoints_and_temperature_threshold_mapping(self):
        source = (ROOT / "kernel/power/qcom_fg.c").read_text()

        def function(name):
            start = source.index("static int " + name + "(")
            stop = source.index("\n}\n", start) + 3
            return source[start:stop]

        functions = [function(name) for name in (
            "qcom_fg_get_capacity", "qcom_fg_get_temp_threshold",
            "qcom_fg_set_temp_threshold", "qcom_fg_gen3_get_temp_threshold")]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
enum power_supply_property { POWER_SUPPLY_PROP_TEMP_MIN, POWER_SUPPLY_PROP_TEMP_MAX,
 POWER_SUPPLY_PROP_TEMP_ALERT_MIN, POWER_SUPPLY_PROP_TEMP_ALERT_MAX };
#define BATT_MONOTONIC_SOC 0x009
#define TEMP_THRESHOLD 0x454
#define BATT_INFO_JEITA_COLD 0x162
#define BATT_INFO_JEITA_COOL 0x163
#define BATT_INFO_JEITA_WARM 0x164
#define BATT_INFO_JEITA_HOT 0x165
#define DIV_ROUND_CLOSEST(n,d) (((n) + (d) / 2) / (d))
#define dev_err(...) ((void)0)
struct qcom_fg_chip { void *dev; };
static u8 raw_soc[2] = { 1, 1 }, raw_temp;
static int observed_offset = -1, sram_error;
static int qcom_fg_read(struct qcom_fg_chip *chip, u8 *out, u16 addr, int len)
{
 (void)chip;
 if (addr == BATT_MONOTONIC_SOC) { memcpy(out, raw_soc, len); return 0; }
 assert(addr == BATT_INFO_JEITA_COLD || addr == BATT_INFO_JEITA_COOL ||
        addr == BATT_INFO_JEITA_WARM || addr == BATT_INFO_JEITA_HOT);
 observed_offset = addr; *out = raw_temp; return 0;
}
static int qcom_fg_sram_read(struct qcom_fg_chip *chip, u8 *out, u16 addr, int len, int offset)
{ (void)chip; assert(addr == TEMP_THRESHOLD && len == 1); observed_offset = offset; *out = raw_temp; return sram_error; }
static int qcom_fg_sram_write(struct qcom_fg_chip *chip, u8 *in, u16 addr, int len, int offset)
{ (void)chip; assert(addr == TEMP_THRESHOLD && len == 1); observed_offset = offset; raw_temp = *in; return sram_error; }
'''
        harness += "\n".join(functions) + r'''
int main(void) {
 struct qcom_fg_chip chip = { 0 };
 int value = -1;
 raw_soc[0] = raw_soc[1] = 0;
 assert(!qcom_fg_get_capacity(&chip, &value) && value == 0);
 raw_soc[0] = raw_soc[1] = 0xff;
 assert(!qcom_fg_get_capacity(&chip, &value) && value == 100);
 raw_soc[0] = raw_soc[1] = 1;
 assert(!qcom_fg_get_capacity(&chip, &value) && value == 1);
 raw_soc[0] = raw_soc[1] = 0xfe;
 assert(!qcom_fg_get_capacity(&chip, &value) && value == 99);
 const int props[] = { POWER_SUPPLY_PROP_TEMP_MIN, POWER_SUPPLY_PROP_TEMP_MAX,
                       POWER_SUPPLY_PROP_TEMP_ALERT_MIN, POWER_SUPPLY_PROP_TEMP_ALERT_MAX };
 const int offsets[] = { 2, 3, 0, 1 };
 for (int i = 0; i < 4; ++i) {
  assert(!qcom_fg_get_temp_threshold(&chip, props[i], &value));
  assert(observed_offset == offsets[i]);
  assert(!qcom_fg_set_temp_threshold(&chip, props[i], 100));
  assert(observed_offset == offsets[i]);
 }
 raw_temp = 111;
 assert(!qcom_fg_gen3_get_temp_threshold(&chip, POWER_SUPPLY_PROP_TEMP_MIN, &value));
 assert(value == 255);
 return 0;
}
'''
        entry = self.source / "fg.c"
        binary = self.source / "fg"
        entry.write_text(harness)
        result = subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(entry), "-o", str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_los_policy_has_no_artificial_soc_cap_and_preserves_safety_boundaries(self):
        harness = r'''
#include <assert.h>
#include "umi_charge_policy.h"
#define D(on,p,h,t,v,s,old,test) umi_charge_policy(on,p,h,t,v,s,4400000,old,test)
int main(void) {
 assert(D(1,1,1,300,4100000,65,0,0) == 1);
 assert(D(1,1,1,300,4100000,70,1,0) == 1);
 assert(D(1,1,1,300,4100000,68,0,0) == 1);
 assert(D(1,1,1,300,4100000,68,1,0) == 1);
 assert(D(1,1,1,300,4100000,78,0,1) == 1);
 assert(D(1,1,1,300,4100000,100,0,1) == 1);
 assert(D(0,1,1,300,4100000,60,1,1) == 0);
 assert(D(1,0,1,300,4100000,60,1,1) == 0);
 assert(D(1,1,0,300,4100000,60,1,1) == 0);
 assert(D(1,1,1,150,4100000,60,1,1) == 0);
 assert(D(1,1,1,151,4100000,60,1,1) == 1);
 assert(D(1,1,1,450,4100000,60,1,1) == 1);
 assert(D(1,1,1,451,4100000,60,1,1) == 0);
 assert(D(1,1,1,-10000,4100000,60,1,1) == 0);
 /* The hardware FV regulator owns the boundary; a BMS sample at FV is not
  * a global charge stop. Invalid telemetry remains fail-closed. */
 assert(D(1,1,1,300,4400000,60,1,1) == 1);
 assert(D(1,1,1,300,4500000,60,1,1) == 1);
 assert(D(1,1,1,300,-1,60,1,1) == 0);
 assert(D(1,1,1,300,4100000,-1,1,1) == 0);
 assert(D(1,1,1,300,4100000,101,1,1) == 0);
 assert(umi_charge_policy(1,1,1,300,4100000,60,4480000,1,1) == 0);
 return 0;
}
'''
        entry = self.source / "policy.c"
        binary = self.source / "policy"
        entry.write_text(harness)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "kernel/power"),
                        str(entry), "-o", str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_worker_extended_window_timeout_unplug_faults_and_explicit_stop(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        functions = []
        for name in ("static int smb5_inhibit_with_fallback(",
                     "static int smb5_get_charge_control_threshold(",
                     "static void smb5_status_change_work(",
                     "static irqreturn_t smb5_handle_usb_plugin(",
                     "static int smb5_get_iio_chan(",
                     "static ssize_t charge_test_store("):
            start = source.index(name)
            stop = source.index("\n}\n", start) + 3
            functions.append(source[start:stop])
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include "umi_charge_policy.h"
typedef int irqreturn_t;
struct iio_channel { int unused; };
#define HZ 1000UL
#define USBIN_CMD_IL 0x340
#define USBIN_SUSPEND_BIT 1
#define BARK_BITE_WDOG_PET 0x643
#define BARK_BITE_WDOG_PET_BIT 1
#define BIT(n) (1U << (n))
#define POWER_SUPPLY_HEALTH_UNKNOWN 0
#define POWER_SUPPLY_HEALTH_GOOD 1
#define POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO 0
#define POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE 1
#define IRQ_HANDLED 1
#define IS_ERR(chan) ((chan) == NULL)
#define PTR_ERR(chan) (-EINVAL)
enum { POWER_SUPPLY_PROP_PRESENT, POWER_SUPPLY_PROP_TEMP,
       POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CAPACITY,
       POWER_SUPPLY_PROP_CURRENT_NOW };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct smb5_chip {
 void *dev, *regmap, *chg_psy;
 unsigned int base, test_seconds;
 struct delayed_work status_change_work;
 int lock, max_fv_uv;
 bool enabled, inhibit, stopping, test_armed, test_saw_disconnect, test_active, pps_owned;
 unsigned long test_deadline;
};
struct device { struct smb5_chip *chip; };
struct device_attribute { int unused; };
static unsigned long jiffies;
static int online = 1, usb_error, battery_error, limit_error, stop_error;
static int values[] = { 1, 300, 4100000, 78, 0 };
static int health = POWER_SUPPLY_HEALTH_GOOD, enabled, feeds, suspends;
static int adc_reads, adc_error;
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define time_after_eq(a,b) ((long)((a) - (b)) >= 0)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define sysfs_streq(a,b) (!strcmp(a,b))
#define dev_get_drvdata(d) ((d)->chip)
#define power_supply_get_drvdata(d) (d)
#define power_supply_changed(d) ((void)(d))
#define mod_delayed_work(q,w,t) ((void)(w))
#define schedule_delayed_work(w,t) ((void)(w))
static void log_message(const char *fmt, ...) { (void)fmt; }
#define dev_info(d,...) log_message(__VA_ARGS__)
#define dev_err(d,...) log_message(__VA_ARGS__)
static int smb5_get_prop_usb_online(struct smb5_chip *c, int *v)
{ (void)c; *v = online; return usb_error; }
static void smb5_log_registers(struct smb5_chip *c) { (void)c; }
static int smb5_battery_value(struct smb5_chip *c, int prop, int *v)
{ (void)c; *v = values[prop]; return battery_error; }
static int smb5_get_prop_health(struct smb5_chip *c, int *v)
{ (void)c; *v = health; return 0; }
static int smb5_limits_verified(struct smb5_chip *c)
{ (void)c; return limit_error; }
static int regmap_update_bits(void *map, unsigned int reg, unsigned int mask, unsigned int v)
{ (void)map; assert(reg == USBIN_CMD_IL && mask == 1); if (v) ++suspends; return 0; }
static int regmap_write(void *map, unsigned int reg, unsigned int v)
{ (void)map; assert(reg == BARK_BITE_WDOG_PET && v == 1); ++feeds; return 0; }
static int smb5_set_prop_charging_enabled(struct smb5_chip *c, unsigned int v)
{ (void)c; if (v == 1 && stop_error) return stop_error; enabled = !v; return 0; }
static int iio_read_channel_processed(struct iio_channel *chan, int *value)
{ (void)chan; ++adc_reads; if (adc_error) return adc_error; *value = 1234; return 0; }
'''
        harness += "\n".join(functions) + r'''
static void tick(struct smb5_chip *c, unsigned long t, int plugged)
{ jiffies = t; online = plugged; smb5_status_change_work(&c->status_change_work.work); }
static void arm(struct smb5_chip *c, const char *command)
{
 struct device d = { c };
 assert(charge_test_store(&d, NULL, command, strlen(command)) == (ssize_t)strlen(command));
 assert(!enabled && !c->enabled);
}
int main(void) {
 struct smb5_chip c = { .inhibit = true, .max_fv_uv = 4400000,
                       .test_seconds = UMI_CHARGE_TEST_SECONDS };
 struct device d = { &c };
 assert(UMI_CHARGE_EXTENDED_TEST_SECONDS == 600);
 tick(&c, 0, 1); assert(!enabled && !feeds);
 assert(charge_test_store(&d, NULL, "6000", 4) == -EINVAL);
 assert(c.inhibit && !c.test_armed);
 arm(&c, "arm-10min"); assert(c.test_seconds == 600);
 tick(&c, 1000, 1); assert(!enabled && !c.test_active);
 tick(&c, 2000, 0); assert(!enabled && c.test_saw_disconnect);
 tick(&c, 3000, 1); assert(enabled && c.test_active && c.test_deadline == 603000);
 assert(smb5_get_charge_control_threshold(&c, true) == 100);
 assert(smb5_get_charge_control_threshold(&c, false) == 0);
 tick(&c, 62000, 1); assert(enabled); /* not the default 60-second timeout */
 tick(&c, 602999, 1); assert(enabled && c.test_active);
 tick(&c, 603000, 1); assert(!enabled && !c.test_active && !c.test_armed);
 tick(&c, 604000, 1); assert(!enabled); /* 78% restores normal inhibition */
 assert(smb5_get_charge_control_threshold(&c, true) == UMI_CHARGE_END_SOC);
 assert(smb5_get_charge_control_threshold(&c, false) == UMI_CHARGE_START_SOC);
 arm(&c, "arm"); assert(c.test_seconds == 60);
 tick(&c, 605000, 0); tick(&c, 606000, 1); assert(enabled);
 tick(&c, 666000, 1); assert(!enabled && !c.test_armed);
 /* jiffies wrap must not extend or shorten the window. */
 arm(&c, "arm-10min");
 tick(&c, ULONG_MAX - 2000, 0); tick(&c, ULONG_MAX - 1000, 1);
 tick(&c, c.test_deadline - 1, 1); assert(enabled);
 tick(&c, c.test_deadline, 1); assert(!enabled && !c.test_armed);
 /* Unplug consumes the insertion; the next computer attachment cannot rearm. */
 arm(&c, "arm-10min"); tick(&c, 0, 0); tick(&c, 1000, 1); assert(enabled);
 tick(&c, 2000, 0); tick(&c, 3000, 1); assert(!enabled && !c.test_armed);
 /* IRQ preserves a disconnect/reconnect that occurs between worker polls. */
 arm(&c, "arm-10min"); online = 0; smb5_handle_usb_plugin(0, &c);
 assert(c.test_saw_disconnect); online = 1; tick(&c, 4000, 1);
 assert(enabled && c.test_active);
 /* Input ADC remains available when USB is present but charging is inhibited. */
 struct iio_channel channel = { 0 };
 int measurement = -1; online = 1; enabled = 0;
 assert(!smb5_get_iio_chan(&c, &channel, &measurement));
 assert(measurement == 1234 && adc_reads == 1);
 online = 0; measurement = -1;
 assert(!smb5_get_iio_chan(&c, &channel, &measurement));
 assert(measurement == 0 && adc_reads == 1);
 online = 1; usb_error = -EIO;
 assert(smb5_get_iio_chan(&c, &channel, &measurement) == -EIO);
 usb_error = 0;
 /* Software stop, sensor/USB/limit failures and overcurrent cancel the test. */
 for (int fault = 0; fault < 5; ++fault) {
  arm(&c, "arm-10min"); tick(&c, 0, 0); tick(&c, 1000, 1); assert(enabled);
  int old_feeds = feeds;
  if (fault == 0) arm(&c, "stop");
  if (fault == 1) battery_error = -EIO;
  if (fault == 2) usb_error = -EIO;
  if (fault == 3) limit_error = -EPERM;
  if (fault == 4) values[POWER_SUPPLY_PROP_CURRENT_NOW] = 6000001;
  tick(&c, 2000, 1);
  assert(!enabled && !c.test_active && !c.test_armed && c.inhibit && feeds == old_feeds);
  battery_error = usb_error = limit_error = 0; values[POWER_SUPPLY_PROP_CURRENT_NOW] = 0;
  tick(&c, 3000, 1); assert(!enabled);
 }
 /* Temperature/telemetry/health gates remain effective during auto charging. */
 arm(&c, "arm-10min"); tick(&c, 0, 0); tick(&c, 1000, 1); assert(enabled);
 values[POWER_SUPPLY_PROP_TEMP] = 451; tick(&c, 2000, 1); assert(!enabled);
 values[POWER_SUPPLY_PROP_TEMP] = 300;
 values[POWER_SUPPLY_PROP_VOLTAGE_NOW] = 4400000; tick(&c, 3000, 1); assert(enabled);
 values[POWER_SUPPLY_PROP_VOLTAGE_NOW] = 0; tick(&c, 3500, 1); assert(!enabled);
 values[POWER_SUPPLY_PROP_VOLTAGE_NOW] = 4100000;
 health = 0; tick(&c, 4000, 1); assert(!enabled); health = 1;
 /* A failed stop uses input suspend and never feeds the watchdog. */
 int old_feeds = feeds;
 stop_error = -EIO;
 assert(charge_test_store(&d, NULL, "stop", 4) == -EIO);
 tick(&c, 5000, 1);
 assert(suspends >= 2 && feeds == old_feeds && c.inhibit && !c.test_armed);
 stop_error = -EIO;
 int old_suspends = suspends;
 assert(smb5_inhibit_with_fallback(&c, "initial") == -EIO);
 assert(suspends == old_suspends + 1);
 return 0;
}
'''
        entry = self.source / "worker.c"
        binary = self.source / "worker"
        entry.write_text(harness)
        result = subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                                 "-I", str(ROOT / "kernel/power"), str(entry), "-o", str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_pm8150b_voltage_encoding_preserves_lower_inherited_limit_and_errors(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        start = source.index("static int smb5_init_hw(")
        stop = source.index("\n}\n", start) + 3
        harness = r'''
#include <assert.h>
#include <errno.h>
#include "umi_charge_policy.h"
#define BIT(n) (1U << (n))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define FLOAT_VOLTAGE_CFG 0x70
#define FAST_CHARGE_CURRENT_CFG 0x61
#define CURRENT_SCALE_FACTOR 50000
#define SNARL_BARK_BITE_WD_CFG 0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT BIT(7)
#define WD_CFG 0x651
#define WDOG_TIMER_EN_BIT BIT(0)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT BIT(1)
#define BARK_WDOG_INT_EN_BIT BIT(6)
struct smb5_chip { void *regmap; unsigned int base; int max_fv_uv; };
static unsigned int registers[0x2000], writes;
static int fail_read;
static int regmap_read(void *m, unsigned int a, unsigned int *v)
{ (void)m; if (fail_read) return -EIO; *v = registers[a]; return 0; }
static int regmap_write(void *m, unsigned int a, unsigned int v)
{ (void)m; assert(a != 0x1042); registers[a] = v; writes++; return 0; }
static int regmap_update_bits(void *m, unsigned int a, unsigned int mask, unsigned int v)
{ return regmap_write(m, a, (registers[a] & ~mask) | (v & mask)); }
static int smb5_set_current_limit(struct smb5_chip *c, unsigned int v)
{ return regmap_write(c->regmap, c->base + 0x370, v / CURRENT_SCALE_FACTOR); }
'''
        harness += source[start:stop] + r'''
int main(void) {
 struct smb5_chip c = { .base = 0x1000 };
 registers[0x1070] = 78;
 assert(!smb5_init_hw(&c));
 assert(c.max_fv_uv == 4380000 && registers[0x1070] == 78);
 assert(registers[0x1061] == 120 && registers[0x1370] == 60);
 assert(registers[0x1653] & BIT(7));
 assert((registers[0x1651] & (BIT(0)|BIT(1)|BIT(6))) == (BIT(0)|BIT(1)|BIT(6)));
 assert(registers[0x1670] & BIT(0));
 registers[0x1070] = 88;
 assert(!smb5_init_hw(&c));
 assert(c.max_fv_uv == 4450000 && registers[0x1070] == 85);
 registers[0x1070] = 0; writes = 0;
 assert(smb5_init_hw(&c) == -ERANGE && writes == 0);
 fail_read = 1;
 assert(smb5_init_hw(&c) == -EIO && writes == 0);
 return 0;
}
'''
        entry = self.source / "encoding.c"
        binary = self.source / "encoding"
        entry.write_text(harness)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "kernel/power"),
                        str(entry), "-o", str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
