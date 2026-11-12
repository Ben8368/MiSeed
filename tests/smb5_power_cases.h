// SPDX-License-Identifier: AGPL-3.0-only
/* Exercise the actual SMB5 worker: finishing input limit and 81/71 hysteresis. */
static void test_smb5_power_and_recharge(void)
{
 struct smb5_chip c = { .max_fv_uv = 4400000 };
 int previous, old_feeds;
 unsigned long t = 0;

 usb_error = battery_error = limit_error = stop_error = status_error = 0;
 health = POWER_SUPPLY_HEALTH_GOOD; charge_status = 0;
 values[POWER_SUPPLY_PROP_TEMP] = 300;
 values[POWER_SUPPLY_PROP_VOLTAGE_NOW] = 4100000;
 values[POWER_SUPPLY_PROP_CURRENT_NOW] = 0;
 input_limit = 3000000;
 values[POWER_SUPPLY_PROP_CAPACITY] = 5;
 tick(&c, t++, 1);
 assert(enabled && input_limit == 2000000 && c.power_stage == 0);
 values[POWER_SUPPLY_PROP_CAPACITY] = 6;
 for (int i = 0; i < 10; i++) tick(&c, t++, 1);
 assert(enabled && input_limit == 3000000 && c.power_stage == 1);
 values[POWER_SUPPLY_PROP_CAPACITY] = 16;
 tick(&c, t++, 1); assert(c.power_stage == 2);
 values[POWER_SUPPLY_PROP_CAPACITY] = 31;
 tick(&c, t++, 1); assert(c.power_stage == 3);
 values[POWER_SUPPLY_PROP_CAPACITY] = 71;
 for (int i = 0; i < 20; i++) {
  previous = input_limit; tick(&c, t++, 1);
  assert(enabled && input_limit == (unsigned int)previous - 100000);
 }
 assert(input_limit == 1000000 && c.power_stage == 4);
 values[POWER_SUPPLY_PROP_CAPACITY] = 70;
 tick(&c, t++, 1); assert(input_limit == 1000000 && c.power_stage == 4);
 values[POWER_SUPPLY_PROP_CAPACITY] = 80;
 tick(&c, t++, 1); assert(enabled && !c.full_bypass);
 values[POWER_SUPPLY_PROP_CAPACITY] = 81;
 old_feeds = feeds;
 tick(&c, t++, 1);
 assert(!enabled && c.full_bypass && feeds == old_feeds && !c.inhibit);
 for (int i = 0; i < 25; i++) tick(&c, t++, 1);
 assert(input_limit == 3000000); /* System input is no longer capped at 5 W. */
 for (int soc = 80; soc >= 72; soc--) {
  values[POWER_SUPPLY_PROP_CAPACITY] = soc;
  tick(&c, t++, 1); assert(!enabled && c.full_bypass);
 }
 /* More than 60 minutes cannot expire the automatic bypass. */
 tick(&c, t + 7200000UL, 1); assert(!enabled && c.full_bypass);
 values[POWER_SUPPLY_PROP_CAPACITY] = 71;
 battery_error = -EIO;
 tick(&c, t++, 1); assert(!enabled && c.full_bypass);
 battery_error = 0; c.cooling_state = 1;
 tick(&c, t++, 1); assert(!enabled && !c.full_bypass && c.power_stage == 4);
 c.cooling_state = 0; c.inhibit = true;
 tick(&c, t++, 1); assert(!enabled);
 c.inhibit = false;
 tick(&c, t++, 1); assert(enabled && input_limit == 1000000); /* 71% resumes 5 W. */
 values[POWER_SUPPLY_PROP_CAPACITY] = 71;
 for (int i = 0; i < 20; i++) tick(&c, t++, 1);
 assert(enabled && input_limit == 1000000 && c.power_stage == 4);
 /* Failed inhibition must not raise the input budget or feed watchdog. */
 values[POWER_SUPPLY_PROP_CAPACITY] = 81;
 stop_error = -EIO; old_feeds = feeds;
 tick(&c, t++, 1);
 assert(!c.enabled && c.full_bypass && input_limit == 1000000 && feeds == old_feeds);
 stop_error = 0;
 tick(&c, t++, 1); assert(!enabled && c.full_bypass);
 /* An external stricter ICL remains authoritative, including bypass. */
 input_limit = 800000;
 tick(&c, t++, 1); assert(input_limit == 800000 && c.full_bypass);
 values[POWER_SUPPLY_PROP_CAPACITY] = 70; /* A skipped 71% sample still uses 5 W. */
 tick(&c, t++, 1); assert(enabled && input_limit == 800000 && !c.full_bypass);
}

static void test_smb5_full_and_unplug(struct smb5_chip *c)
{
 /* Hardware FULL still terminates before the SOC cap. Status failures stop;
  * unplug clears only the automatic session latch, never user inhibit. */
 values[POWER_SUPPLY_PROP_CAPACITY] = 80;
 tick(c, 4700, 1); assert(enabled && !c->full_bypass);
 status_error = -EIO;
 tick(c, 4710, 1); assert(!enabled && !c->full_bypass);
 status_error = 0; charge_status = POWER_SUPPLY_STATUS_FULL;
 tick(c, 4720, 1); assert(!enabled && c->full_bypass && !c->inhibit);
 charge_status = 0;
 int bypass_suspends = suspends, bypass_feeds = feeds;
 for (int i = 0; i < 20; i++) tick(c, 4730 + i, 1);
 assert(!enabled && c->full_bypass && suspends == bypass_suspends && feeds == bypass_feeds);
 assert(c->power_stage == UMI_WIRED_BYPASS_STAGE && input_limit == 3000000);
 tick(c, 4800, 0); assert(!c->full_bypass);
 tick(c, 4900, 1); assert(enabled);
 /* An IRQ also preserves a brief unplug/replug between worker polls. */
 c->full_bypass = true; online = 0; smb5_handle_usb_plugin(0, c);
 assert(!c->full_bypass); tick(c, 4901, 1); assert(enabled);
 charge_status = POWER_SUPPLY_STATUS_FULL; stop_error = -EIO;
 bypass_feeds = feeds; bypass_suspends = suspends;
 tick(c, 4902, 1);
 assert(c->full_bypass && !c->enabled && feeds == bypass_feeds && suspends > bypass_suspends);
 stop_error = 0; charge_status = 0;
 tick(c, 4903, 1); assert(c->full_bypass && !enabled);
 c->inhibit = true; c->full_bypass = true;
 tick(c, 4910, 0); assert(!c->full_bypass && c->inhibit);
}
