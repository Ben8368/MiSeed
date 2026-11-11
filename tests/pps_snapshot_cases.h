// SPDX-License-Identifier: AGPL-3.0-only
/* Keep consumed fault evidence even when a later register read fails. */
static void test_partial_fault_snapshot(void)
{
	struct umi_pps_sample sample = {0};

	reset();
	assert(!umi_pps_prepare(&c));
	regs[0x10] = 0x20;
	regs[0x11] = 0x40;
	io_count = 0;
	fail_at = c.nprotection + 3; /* Fail alarm read after fault + latch. */
	assert(umi_pps_sample(&c, &sample) == -EIO);
	assert(sample.fault_raw == 0x20 && sample.latched_raw == 0x40);
}

/* Synthetic response curve, not an electrical model or Umi calibration. */
static void power_tick(void)
{
	int ibus = 2000 + (c.request_mv - 8400) * 2;

	adc(0, ibus);
	adc(1, c.request_mv);
	bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW] = (ibus * 2 - 500) * 1000;
	clock_ms += UMI_PPS_PERIOD_MS;
	tick();
}

static void test_power_policy(void)
{
	struct umi_pps_sample s = {.soc = 15, .thermal_bat_ma = 6000,
		.vbus_mv = 9000, .ibus_ma = 2000};
	struct umi_pps_power p = {0};
	struct umi_pps_caps caps = {3300, 11000, 3000};
	int previous, requests;

	umi_pps_power_update(&p, &s, 37999, 100);
	assert(p.target_mw == 10000 && !p.steady && p.filtered_mw == 18000);
	umi_pps_power_update(&p, &s, 37999, 1099);
	assert(p.target_mw == 10000);
	for (int i = 1; i <= 17; i++) {
		umi_pps_power_update(&p, &s, 37999, 100 + i * 1000);
		assert(p.target_mw == 10000 + i * 1000 && !p.steady);
	}
	/* Delayed I/O permits one step, not a catch-up leap. */
	s.soc = 30;
	umi_pps_power_update(&p, &s, 37999, 60000);
	assert(p.steady && p.target_mw == 26000);
	s.soc = 29;
	for (int i = 1; i <= 8; i++) {
		previous = p.target_mw;
		umi_pps_power_update(&p, &s, 35000, 60000 + i * 1000);
		assert(p.target_mw == previous - 1000 && p.steady);
	}
	umi_pps_power_update(&p, &s, 35000, 100000);
	assert(p.target_mw == 18000 && p.steady);
	s.soc = 80;
	umi_pps_power_update(&p, &s, 35000, 101000);
	assert(p.target_mw == 18000 && p.stage == 2);
	s.soc = 81;
	for (int i = 1; i <= 9; i++)
		umi_pps_power_update(&p, &s, 35000, 101000 + i * 1000);
	assert(p.target_mw == 9000 && p.stage == 3);
	s.soc = 79;
	umi_pps_power_update(&p, &s, 35000, 120000);
	assert(p.target_mw == 9000 && p.stage == 3);
	p = (struct umi_pps_power){0};
	s.soc = 14;
	umi_pps_power_update(&p, &s, 35000, 100);
	assert(p.target_mw == 10000 && p.stage == 0);
	s.soc = 15;
	umi_pps_power_update(&p, &s, 35000, 1100);
	assert(p.target_mw == 11000 && p.stage == 1);
	s.soc = 14;
	umi_pps_power_update(&p, &s, 35000, 2100);
	assert(p.target_mw == 12000 && p.stage == 1);
	p = (struct umi_pps_power){0};
	s.soc = 20;
	umi_pps_power_update(&p, &s, 38000, 100);
	assert(p.steady && p.target_mw == 10000);
	p = (struct umi_pps_power){0};
	s.thermal_bat_ma = 4100;
	umi_pps_power_update(&p, &s, 35000, 100);
	assert(p.steady);
	/* Deadband holds voltage; power correction never overrides a FV reduction. */
	s = (struct umi_pps_sample){.vbat_mv = 4000, .pump_vbat_mv = 4000,
		.ibat_ma = 3000, .ibus_ma = 2000, .thermal_bat_ma = 6000};
	p.target_mw = 18000;
	p.filtered_mw = 18000;
	assert(umi_pps_power_next_mv(&s, &caps, 4450, 3000, 8400, &p) == 8400);
	p.filtered_mw = 18501;
	assert(umi_pps_power_next_mv(&s, &caps, 4450, 3000, 8400, &p) == 8380);
	p.filtered_mw = 17499;
	assert(umi_pps_power_next_mv(&s, &caps, 4450, 3000, 8400, &p) == 8420);
	s.pump_vbat_mv = 4430;
	assert(umi_pps_power_next_mv(&s, &caps, 4450, 3000, 8400, &p) == 8380);
	/* Real worker: boost saturates at existing current limits, then ramps down
	 * without a PDO transition or a return to boost when SOC drops back.
	 */
	reset();
	bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 20;
	run();
	for (int i = 0; i < 100; i++) {
		power_tick();
		assert(c.state == PPS_RUN && c.request_ma <= 3000 && c.request_mv <= 9500);
	}
	assert(c.power.target_mw == 27000 && !c.power.steady);
	bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 30;
	for (int i = 0; i < 120; i++) {
		previous = c.power.target_mw;
		power_tick();
		assert(c.state == PPS_RUN && c.power.target_mw <= previous);
	}
	assert(c.power.target_mw == 18000 && c.power.steady && !fixed_count);
	requests = voltage_requests;
	bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 29;
	for (int i = 0; i < 50; i++) {
		power_tick();
		assert(c.state == PPS_RUN && c.power.target_mw == 18000);
		assert(c.power.filtered_mw >= 17500 && c.power.filtered_mw <= 18500);
	}
	assert(voltage_requests <= requests + 2); /* Stable input, no repeated AMS. */
	/* A tighter thermal request is staged: lower VBUS before limiting IBUS.
	 * Raw battery-current violations still stop in the existing hard gate.
	 */
	c.cooling_state = 8;
	power_tick();
	assert(c.state == PPS_RUN && c.request_ma == 3000);
	for (int i = 0; i < 30; i++) {
		power_tick();
		assert(c.state == PPS_RUN);
	}
	assert(c.request_ma == 2050 && !fixed_count);
	bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW] = 4101000;
	tick();
	assert(c.state == PPS_FAULT && !claim_owned);
	/* A still-attached user stop cannot clear the boost-exit latch. */
	reset();
	bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 31;
	run();
	assert(c.power.steady);
	assert(!umi_pps_stop(&c, PPS_OFF, 0));
	assert(c.power.steady);
	pd.prop[POWER_SUPPLY_PROP_ONLINE] = 0;
	sw.prop[POWER_SUPPLY_PROP_ONLINE] = 1;
	tick();
	assert(c.power.steady); /* TCPM offline alone is not a physical unplug. */
	sw.prop[POWER_SUPPLY_PROP_ONLINE] = 0;
	tick();
	assert(!c.power.target_mw && !c.power.steady);
	/* Actual worker can regulate below 2 A measured current without changing
	 * the existing 2 A minimum requested source-current limit.
	 */
	{
		reset();
		bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 81;
		run();
		for (int i = 0; i < 120; i++) {
			power_tick();
			assert(c.state == PPS_RUN && c.power.target_mw == 9000);
		}
		assert(c.power.filtered_mw >= 8500 && c.power.filtered_mw <= 9500);
		assert(!fixed_count && c.request_ma >= 2000);
	}
	reset();
	bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 14;
	begin();
	assert(c.state == PPS_WAIT_PLUG && !enable_count && !claim_owned && !pps_activations);
	bms.prop[POWER_SUPPLY_PROP_CAPACITY] = 15;
	tick();
	assert(c.state == PPS_SETTLE && c.power.target_mw == 10000);
	clock_ms += 200;
	tick();
	assert(c.state == PPS_RUN && enable_count == 1);
}
