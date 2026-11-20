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
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#ifndef CONFIG_MISEED_RX_DIAGNOSTIC
#define CONFIG_MISEED_RX_DIAGNOSTIC 1
#endif
#ifndef CONFIG_MISEED_RX_NFC_COEX
#define CONFIG_MISEED_RX_NFC_COEX 0
#endif
#ifndef CONFIG_MISEED_RX_ENABLE_EXPERIMENT
#define CONFIG_MISEED_RX_ENABLE_EXPERIMENT 0
#endif
#define IS_ENABLED(n) (n)
enum power_supply_property {
    POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW,
};
union power_supply_propval { int intval; };
struct fake_bus { unsigned int raw, reg; int reads, error; };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
#define container_of(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))
#define to_delayed_work(ptr) container_of(ptr, struct delayed_work, work)
#define msecs_to_jiffies(ms) (ms)
struct rx1619 {
    void *dev;
    struct fake_bus *regmap;
    int *power_good;
    struct delayed_work pg_trace;
    unsigned int trace_left;
};
struct power_supply { struct rx1619 *rx; };
static int gpio_second, override_second, gpio_reads, pg_samples, last_pg;
static unsigned int reschedules;
static char message[100];
static void dev_info(void *dev, const char *fmt, ...) {
    va_list args;
    (void)dev;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
}
static void schedule_delayed_work(struct delayed_work *work, unsigned int delay) {
    assert(work && delay == 2000);
    reschedules++;
}
static void rx1619_log_pg_sample(struct rx1619 *rx, int pg) {
    assert(rx->power_good && *rx->power_good == pg);
    pg_samples++;
    last_pg = pg;
}
static struct rx1619 *power_supply_get_drvdata(struct power_supply *s) { return s->rx; }
static int gpiod_get_value_cansleep(int *state) {
    gpio_reads++;
    return gpio_reads == 2 && override_second ? gpio_second : *state;
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
static void test_properties(void) {
    int pg = 0;
    struct fake_bus bus = { .raw = 255 };
    struct rx1619 rx = { .power_good = &pg, .regmap = &bus };
    struct power_supply supply = { .rx = &rx };
    union power_supply_propval value = { .intval = -1 };
    assert(!rx1619_get_property(&supply, POWER_SUPPLY_PROP_ONLINE, &value));
    assert(value.intval == 0 && !bus.reads && pg_samples == CONFIG_MISEED_RX_DIAGNOSTIC && last_pg == 0);
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value) == -ENODATA);
    assert(!bus.reads);
    pg = -EIO;
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_ONLINE, &value) == -EIO);
    assert(!bus.reads && pg_samples == 2 * CONFIG_MISEED_RX_DIAGNOSTIC);
    assert(last_pg == (CONFIG_MISEED_RX_DIAGNOSTIC ? -EIO : 0));
    pg = 1;
    assert(!rx1619_get_property(&supply, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value));
    assert(value.intval == 22412109 && bus.reg == 0x09);
    assert(!rx1619_get_property(&supply, POWER_SUPPLY_PROP_CURRENT_NOW, &value));
    assert(value.intval == 2490234 && bus.reg == 0x0b);
    bus.error = -ENXIO;
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value) == -ENXIO);
    assert(value.intval == 2490234);
    bus.error = gpio_reads = 0;
    override_second = 1;
    assert(rx1619_get_property(&supply, POWER_SUPPLY_PROP_CURRENT_NOW, &value) == -ENODATA);
    assert(value.intval == 2490234);
}

static void test_diagnostic_trace(void) {
    int pg = 0;
    struct fake_bus bus = { .raw = 0x3b };
    struct rx1619 rx = { .power_good = &pg, .regmap = &bus, .trace_left = RX1619_TRACE_SAMPLES };
    gpio_reads = override_second = 0;
    rx1619_trace_power_good(&rx.pg_trace.work);
    assert(bus.reads == 1 && bus.reg == 0x09 && gpio_reads == 2);
    assert(!strcmp(message, "PG trace pg=0 pg_after=0 reg09_raw=0x3b\n"));
    gpio_reads = 0;
    override_second = gpio_second = 1;
    rx1619_trace_power_good(&rx.pg_trace.work);
    assert(!strcmp(message, "PG trace pg=0 pg_after=1 reg09_raw=0x3b\n"));
    gpio_reads = 0;
    gpio_second = -EIO;
    rx1619_trace_power_good(&rx.pg_trace.work);
    assert(!strcmp(message, "PG trace pg=0 pg_after=-5 reg09_raw=0x3b\n"));
    bus.error = -ENXIO;
    rx1619_trace_power_good(&rx.pg_trace.work);
    assert(!strcmp(message, "PG trace pg=0 reg09_error=-6\n"));
    pg = -EIO;
    rx1619_trace_power_good(&rx.pg_trace.work);
    assert(bus.reads == 4 && !strcmp(message, "PG trace pg_error=-5\n"));
    pg = 1;
    for (unsigned int i = 5; i < RX1619_TRACE_SAMPLES; i++)
        rx1619_trace_power_good(&rx.pg_trace.work);
    assert(bus.reads == 4 && rx.trace_left == 0 && reschedules == RX1619_TRACE_SAMPLES - 1);
}

int main(void) {
    assert(RX1619_TRACE_ENABLED == EXPECT_TRACE);
    test_properties();
    test_diagnostic_trace();
}
"""


class Rx1619Test(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_units_offline_and_lost_power_good(self):
        driver = (ROOT / "kernel/power/rx1619.c").read_text(encoding="utf-8")
        constants = driver[driver.index("#define RX1619_VOUT"):driver.index("struct rx1619 {")]
        functions = driver[driver.index("static void rx1619_trace_power_good("):
                           driver.index("static const struct regmap_config")]
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / "rx.c", Path(directory) / "rx"
            source.write_text(SHIM + constants + functions + CASES, encoding="utf-8")
            for profile, flags in {
                "observe": ["-DEXPECT_TRACE=1"],
                "normal": ["-DEXPECT_TRACE=0", "-DCONFIG_MISEED_RX_DIAGNOSTIC=0"],
                "coex": ["-DEXPECT_TRACE=0", "-DCONFIG_MISEED_RX_NFC_COEX=1"],
                "enable": ["-DEXPECT_TRACE=0", "-DCONFIG_MISEED_RX_ENABLE_EXPERIMENT=1"],
            }.items():
                with self.subTest(profile=profile):
                    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                                    source, "-o", binary], check=True)
                    subprocess.run([binary], check=True)
