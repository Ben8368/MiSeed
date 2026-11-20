"""Replay RX1619 read-only properties with synthetic I2C and power-good."""

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
#include <stdint.h>
typedef uint64_t u64;
enum power_supply_property {
    POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW,
};
union power_supply_propval { int intval; };
struct fake_bus { unsigned int raw, reg; int reads, error; };
struct rx1619 { struct fake_bus *regmap; int *power_good; };
struct power_supply { struct rx1619 *rx; };
static int drop, gpio_reads;
static struct rx1619 *power_supply_get_drvdata(struct power_supply *s) { return s->rx; }
static int gpiod_get_value_cansleep(int *state) {
    gpio_reads++;
    return gpio_reads == 2 && drop ? 0 : *state;
}
static int regmap_read(struct fake_bus *bus, unsigned int reg, unsigned int *raw) {
    bus->reads++;
    bus->reg = reg;
    if (bus->error)
        return bus->error;
    *raw = bus->raw;
    return 0;
}
"""
CASES = r"""
int main(void) {
    int pg = 0;
    struct fake_bus bus = { .raw = 255 };
    struct rx1619 rx = { .power_good = &pg, .regmap = &bus };
    struct power_supply supply = { .rx = &rx };
    union power_supply_propval value = { .intval = -1 };
    assert(!rx1619_get_property(&supply, POWER_SUPPLY_PROP_ONLINE, &value));
    assert(value.intval == 0 && !bus.reads);
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value) == -ENODATA);
    assert(!bus.reads);
    pg = -EIO;
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_ONLINE, &value) == -EIO);
    assert(!bus.reads);
    pg = 1;
    assert(!rx1619_get_property(&supply, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value));
    assert(value.intval == 22412109 && bus.reg == 0x09);
    assert(!rx1619_get_property(&supply, POWER_SUPPLY_PROP_CURRENT_NOW, &value));
    assert(value.intval == 2490234 && bus.reg == 0x0b);
    bus.error = -ENXIO;
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value) == -ENXIO);
    assert(value.intval == 2490234);
    bus.error = gpio_reads = 0;
    drop = 1;
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_CURRENT_NOW, &value) == -ENODATA);
    assert(value.intval == 2490234);
}
"""


class Rx1619Test(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_units_offline_and_lost_power_good(self):
        driver = (ROOT / "kernel/power/rx1619.c").read_text(encoding="utf-8")
        constants = driver[driver.index("#define RX1619_VOUT"):driver.index("struct rx1619 {")]
        function = driver[driver.index("static int rx1619_get_property("):
                          driver.index("static const struct regmap_config")]
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / "rx.c", Path(directory) / "rx"
            source.write_text(SHIM + constants + function + CASES, encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", source,
                            "-o", binary], check=True)
            subprocess.run([binary], check=True)
