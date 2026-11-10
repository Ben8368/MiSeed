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


class FuelGaugeTests(PowerPortFixture):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_fg_standard_current_sign_and_read_errors(self):
        source = power_source("qcom_fg")
        start = source.index("int qcom_fg_current_now(")
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


    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_fg_capacity_endpoints_and_temperature_threshold_mapping(self):
        source = power_source("qcom_fg")

        def function(name):
            start = source.index("int " + name + "(")
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


if __name__ == "__main__":
    unittest.main()
