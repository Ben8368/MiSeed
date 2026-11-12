// SPDX-License-Identifier: AGPL-3.0-only
/* Simulate the controller and cable state when a detach races a PPS read. */
static void pps_detach_during_online_read(struct power_supply *p,
		enum power_supply_property prop, int *reads_left)
{
	if (p != &pd || prop != POWER_SUPPLY_PROP_ONLINE || !*reads_left ||
	    --*reads_left)
		return;
	p->prop[POWER_SUPPLY_PROP_ONLINE] = 0;
	regs[0x0c] &= ~BIT(7);
	regs[0x0a] &= ~BIT(2);
	regs[0x0d] &= ~BIT(2); /* VBUS-present falls, VBAT-present remains. */
	/* ADCs can retain the last conversion while CHG_EN has already fallen. */
}

static void test_detach_during_request(void)
{
	reset();
	run();
	disconnect_after_online_reads = 5;
	clock_ms += 200;
	tick();
	assert(c.state == PPS_WAIT_PLUG && c.last_error == 0 && !claim_owned &&
	       !c.stop_pending && !(regs[0x0c] & BIT(7)) && release_count == 1);
	/* A new source can start a new PPS session after clean detach. */
	regs[0x0d] |= BIT(2);
	begin();
	assert(c.state == PPS_SETTLE && enable_count == 1);
	clock_ms += 200;
	tick();
	assert(c.state == PPS_RUN && enable_count == 2);

	/* ONLINE=0 cannot hide a sensor error, a latch, a protection mismatch,
	 * a timeout, or an absolute limit violation from the same control cycle.
	 */
	for (int fault = 0; fault < 6; fault++) {
		reset();
		run();
		disconnect_after_online_reads = 2;
		if (fault == 0) fail_sensor = 1;
		if (fault == 1) regs[0x11] = BIT(6);
		if (fault == 2) bad_readback = 1;
		if (fault == 3) thermal_delay_ms = 41;
		if (fault == 4) adc(0, -1);
		if (fault == 5) adc(4, 3499);
		tick();
		assert(c.state == PPS_FAULT && c.last_error < 0 &&
		       !claim_owned && !c.stop_pending && !(regs[0x0c] & BIT(7)));
	}
	/* Stop failure retains exclusive ownership and retries before rearming. */
	reset();
	run();
	disconnect_after_online_reads = 5;
	fail_stop = 1;
	tick();
	assert(c.state == PPS_FAULT && c.stop_pending && claim_owned);
	fail_stop = 0;
	tick();
	assert(c.state == PPS_FAULT && !c.stop_pending && !claim_owned);
	tick();
	assert(c.state == PPS_WAIT_PLUG);
	/* TCPM may report loss while entering PPS or refreshing a RUN request. */
	for (int running = 0; running < 2; running++) {
		reset();
		if (running) run();
		fail_request = 2;
		disconnect_after_online_reads = running ? 4 : 3;
		if (running) { clock_ms=c.last_pps_request_ms+UMI_PPS_REFRESH_MS; tick(); } else begin();
		assert(c.state == PPS_WAIT_PLUG && !c.last_error && !claim_owned &&
		       !c.pps && !c.stop_pending && !(regs[0x0c] & BIT(7)));
	}
	reset();
	run();
	fail_request = 2;
	clock_ms=c.last_pps_request_ms+UMI_PPS_REFRESH_MS;
	tick();
	assert(c.state == PPS_FAULT && c.last_error == -ENOTCONN && !claim_owned);
	/* Reproduce 150.775s: battery ONLINE is stale while BQ has stopped.
	 * Only a fresh TCPM offline read makes this a normal detach.
	 */
	for (int offline = 0; offline < 2; offline++) {
		reset();
		run();
		regs[0x0c] &= ~BIT(7);
		regs[0x0a] = 0;
		regs[0x0d] = BIT(1);
		disconnect_after_online_reads = offline ? 4 : 0;
		tick();
		assert(c.state == (offline ? PPS_WAIT_PLUG : PPS_FAULT));
		assert(c.last_error == (offline ? 0 : -ERANGE));
		assert(!claim_owned && !c.stop_pending);
	}
}
