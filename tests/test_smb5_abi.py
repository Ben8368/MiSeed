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


from power_test_support import PowerPortFixture, power_source


class Smb5AbiTests(PowerPortFixture):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_smb5_sink_current_converts_adc_voltage_and_rejects_boost(self):
        source = power_source("qcom_pm8150b_charger")
        start = source.index("int smb5_get_input_current(")
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
    def test_standard_input_limit_and_behaviour_failure_paths(self):
        source = power_source("qcom_pm8150b_charger")
        functions = []
        for name in ("smb5_set_current_limit", "smb5_set_property", "smb5_property_is_writable"):
            start = source.rindex("int " + name + "(")
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


    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_standard_behaviour_enum_cannot_reverse_enable_or_accept_force_discharge(self):
        source = power_source("qcom_pm8150b_charger")
        functions = []
        for name in ("smb5_get_prop_charging_enabled", "smb5_set_prop_charging_enabled"):
            start = source.index("int " + name + "(")
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
    def test_pm8150b_voltage_encoding_preserves_lower_inherited_limit_and_errors(self):
        source = power_source("qcom_pm8150b_charger")
        start = source.index("int smb5_init_hw(")
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
