"""Offline safety boundaries for the unqualified FG/SMB5 compile port."""

# SPDX-License-Identifier: AGPL-3.0-only

import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


from power_test_support import PowerPortFixture, power_source


class ChargePolicyTests(PowerPortFixture):
    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_soc_cap_preserves_los_electrical_and_thermal_boundaries(self):
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
 assert(D(1,1,1,300,4100000,80,0,1) == 1);
 assert(D(1,1,1,300,4100000,81,0,1) == 0);
 assert(D(1,1,1,300,4100000,100,0,1) == 0);
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
        source = power_source("qcom_pm8150b_charger")
        functions = []
        for name in ("int smb5_inhibit_with_fallback(",
                     "int smb5_power_limit(",
                     "int smb5_get_charge_control_threshold(",
                     "void smb5_status_change_work(",
                     "irqreturn_t smb5_handle_usb_plugin(",
                     "int smb5_get_iio_chan(",
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
#include "umi_pps_policy.h"
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
#define POWER_SUPPLY_STATUS_FULL 4
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
 bool full_bypass;
 int power_stage;
 unsigned int policy_icl_ua, saved_icl_ua;
 unsigned long test_deadline;
 unsigned long cooling_state;
};
struct device { struct smb5_chip *chip; };
struct device_attribute { int unused; };
static unsigned long jiffies;
static int online = 1, usb_error, battery_error, limit_error, stop_error;
static int values[] = { 1, 300, 4100000, 78, 0 };
static int health = POWER_SUPPLY_HEALTH_GOOD, enabled, feeds, suspends;
static int charge_status, status_error;
static unsigned int input_limit = 3000000;
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
static int smb5_get_prop_status(struct smb5_chip *c, int *v)
{ (void)c; *v = charge_status; return status_error; }
static int smb5_limits_verified(struct smb5_chip *c)
{ (void)c; return limit_error; }
static int smb5_get_current_limit(struct smb5_chip *c, unsigned int *v)
{ (void)c; *v = input_limit; return 0; }
static int smb5_set_current_limit(struct smb5_chip *c, unsigned int v)
{ (void)c; input_limit = v; return 0; }
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
#include "smb5_power_cases.h"
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
 assert(smb5_get_charge_control_threshold(&c, true) == 81);
 assert(smb5_get_charge_control_threshold(&c, false) == 71);
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
 /* Thermal requests survive user auto, unplug/replug and PPS release.
  * Clearing cooling removes only that request; all independent gates apply. */
 c.test_armed = c.test_active = false; c.inhibit = false;
 c.cooling_state = 1; tick(&c, 4100, 1); assert(!enabled);
 tick(&c, 4200, 0); tick(&c, 4300, 1); assert(!enabled && c.cooling_state == 1);
 c.cooling_state = 0; c.inhibit = true;
 tick(&c, 4400, 1); assert(!enabled);
 c.inhibit = false; values[POWER_SUPPLY_PROP_TEMP] = 451;
 tick(&c, 4500, 1); assert(!enabled);
 values[POWER_SUPPLY_PROP_TEMP] = 300;
 tick(&c, 4600, 1); assert(enabled);
 test_smb5_full_and_unplug(&c);
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
 stop_error = 0;
 test_smb5_power_and_recharge();
 return 0;
}
'''
        entry = self.source / "worker.c"
        binary = self.source / "worker"
        entry.write_text(harness)
        result = subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                                 "-I", str(ROOT / "kernel/power"), "-I", str(ROOT / "tests"),
                                 str(entry), "-o", str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
