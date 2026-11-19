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
	fail_at = chip.nprotection + 3; /* Fail alarm read after fault + latch. */
	assert(umi_pps_sample(&c, &sample) == -EIO);
	assert(sample.fault_raw == 0x20 && sample.latched_raw == 0x40);
}

/* Synthetic response curve, not Umi electrical calibration. */
static void power_tick(void)
{
 int ibus = min(c.request_ma, 2000 + (c.request_mv - 8400) * 2);
 if (ibus < 0) ibus = 0;
 adc(0, ibus); adc(1, c.request_mv);
 bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW] = (ibus * 2 - 500) * 1000;
 clock_ms += UMI_PPS_PERIOD_MS;
 tick();
}

static void test_power_policy(void)
{
 struct umi_pps_sample s = {.soc=11, .thermal_bat_ma=6000,
  .vbus_mv=9000, .ibus_ma=2000, .vbat_mv=4000, .pump_vbat_mv=4000};
 struct umi_pps_power p = {0};
 struct umi_pps_caps caps = {3300, 11000, 3000};
 umi_pps_power_update(&p,&s,35000,100);
 assert(p.target_mw==30000 && !p.steady);
 assert(umi_pps_request_current(&s,&caps,&p)==3000);
 assert(umi_pps_current(&caps,1200)==1200);
 assert(umi_pps_current(&caps,999)<0 && umi_pps_current(&caps,3001)<0);
 /* A fixed-PD source has no PPS APDO: keep SMB5 charging without claiming,
  * suspending input, or probing ONLINE=PPS_PROGRAMMABLE.
  */
 reset();pd.prop[POWER_SUPPLY_PROP_USB_TYPE]=POWER_SUPPLY_USB_TYPE_PD;
 begin();
 assert(c.state==PPS_WAIT_PLUG && !claim_owned && !suspended &&
        !pps_activations && !release_count);
 for(int i=1;i<=12;i++)umi_pps_power_update(&p,&s,38000,100+i*1000);
 assert(p.steady && p.target_mw==18000);
 for(int i=13;i<=25;i++)umi_pps_power_update(&p,&s,35000,100+i*1000);
 assert(!p.steady && p.target_mw==30000);
 for(int soc=71;soc<=100;soc++) {
  reset();bms.prop[POWER_SUPPLY_PROP_CAPACITY]=soc;begin();
  assert(c.state==PPS_WAIT_PLUG && !claim_owned && !pps_activations);
 }
 reset();bms.prop[POWER_SUPPLY_PROP_CAPACITY]=10;begin();
 assert(c.state==PPS_WAIT_PLUG && !claim_owned);
 bms.prop[POWER_SUPPLY_PROP_CAPACITY]=11;tick();
 assert(c.state==PPS_SETTLE && c.power.target_mw==30000 && c.request_ma==3000);
 reset();run();
 for(int i=0;i<120;i++)power_tick();
 assert(c.state==PPS_RUN && c.request_ma<=3000 && !fixed_count);
 bms.prop[POWER_SUPPLY_PROP_CAPACITY]=71;power_tick();
 assert(c.state==PPS_WAIT_PLUG && !claim_owned && fixed_count==1);
 smb_stage=2;bms.prop[POWER_SUPPLY_PROP_CAPACITY]=70;tick();
 assert(c.state==PPS_WAIT_PLUG && !claim_owned); /* Fixed-PD recharge only. */
 reset();run();bms.prop[POWER_SUPPLY_PROP_CAPACITY]=80;tick();
 assert(c.state==PPS_WAIT_PLUG && smb_bypass && !claim_owned);
 /* Trip jumps stop immediately without dropping the PPS contract. */
 reset();run();bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=3187000;
 c.cooling_state=11;tick();
 assert(c.state==PPS_THERMAL && claim_owned && !fixed_count && !(regs[0x0c]&BIT(7)));
 clock_ms+=200;tick();assert(c.state==PPS_SETTLE && c.request_ma==1200);
 clock_ms+=200;tick();assert(c.state==PPS_RUN && c.request_ma==1200);
 c.cooling_state=0;
 for(int i=0;i<30;i++){clock_ms+=1000;tick();}
 assert(c.state==PPS_RUN && c.request_ma>1200 && c.request_ma<=3000);
 /* Below the PPS current-limit region: stay paused, refresh, then recover. */
 reset();run();c.cooling_state=15;tick();
 assert(c.state==PPS_THERMAL && claim_owned && !fixed_count);
 int requests=voltage_requests;
 for(int i=0;i<60;i++){clock_ms+=200;tick();assert(c.state==PPS_THERMAL);}
 assert(voltage_requests>requests && enable_count==1);
 c.cooling_state=0;clock_ms+=200;tick();assert(c.state==PPS_SETTLE);
 clock_ms+=200;tick();assert(c.state==PPS_RUN && !fixed_count);
 reset();c.cooling_state=15;begin();
 assert(c.state==PPS_THERMAL && !enable_count && claim_owned);
 fail_sensor=1;tick();assert(c.state==PPS_FAULT && smb_fault && !claim_owned);
 reset();run();c.cooling_state=11;fail_stop=1;
 bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=3187000;tick();
 assert(c.state==PPS_FAULT && c.stop_pending && claim_owned);
 fail_stop=0;tick();assert(!claim_owned && smb_fault);
 /* A paused pump does not exempt a VBUS/request mismatch. */
 reset();c.cooling_state=15;begin();adc(1,c.request_mv+201);tick();
 assert(c.state==PPS_FAULT && smb_fault);
 /* Cleanup with reason=0 must preserve a failed stop's fault lock. */
 reset();run();fail_stop=1;
 assert(umi_pps_stop(&c,PPS_OFF,0)==-EIO && claim_owned);
 fail_stop=0;assert(!umi_pps_stop(&c,PPS_OFF,0) && smb_fault);
 /* Another fault at the same thermal edge is never treated as a pause. */
 reset();run();c.cooling_state=11;regs[0x11]=1;tick();
 assert(c.state==PPS_FAULT && smb_fault && !claim_owned);
 reset();run();c.cooling_state=11;bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=6001000;tick();
 assert(c.state==PPS_FAULT && smb_fault);
}

/* Generic chip semantics are independent of PD and Umi policy state. */
static void test_chip_boundary(void)
{
	struct bq25970_sample sample = {0};
	union power_supply_propval value;
	int before;

	reset();chip.owner = false;
	assert(bq25970_prepare(&chip) == -EPERM);
	assert(bq25970_start(&chip) == -EPERM && !enable_count);
	chip.nprotection = 0;chip.owner = true;
	assert(bq25970_prepare(&chip) == -EPERM);
	assert(!bq25970_read_sample(&chip, &sample));
	drvdata = &chip;
	assert(bq25970_get_property(&sw, POWER_SUPPLY_PROP_CURRENT_NOW, &value) == -ENODATA);
	assert(bq25970_get_property(&sw, POWER_SUPPLY_PROP_USB_TYPE, &value) == -EINVAL);
	assert(bq25970_get_property(&sw, POWER_SUPPLY_PROP_ONLINE, &value) == -EINVAL);

	reset();assert(!umi_pps_prepare(&c));
	regs[0x11] = 0x40;
	assert(!bq25970_read_sample(&chip, &sample) && sample.hardware_fault);
	regs[0x11] = 0; /* Another query must not erase an already consumed fault. */
	drvdata = &chip;before = io_count;
	for (int i = 0; i < 10; i++) {
		assert(!bq25970_get_property(&sw, POWER_SUPPLY_PROP_HEALTH, &value));
		assert(value.intval == POWER_SUPPLY_HEALTH_UNSPEC_FAILURE);
	}
	assert(io_count == before);
	assert(!bq25970_read_sample(&chip, &sample) && sample.latched == 0x40);
	/* Only a new stopped preparation acknowledges the previous session. */
	assert(!bq25970_prepare(&chip));
	adc(0, 1234);adc(1, 8765);adc(4, 4321);
	assert(!bq25970_read_sample(&chip, &sample));
	assert(sample.ibus_ua == 1234000 && sample.vbus_uv == 8765000 && sample.vbat_uv == 4321000);
	assert(!sample.hardware_fault);
	assert(!bq25970_get_property(&sw, POWER_SUPPLY_PROP_HEALTH, &value));
	assert(value.intval == POWER_SUPPLY_HEALTH_GOOD);
	/* Verification failures invalidate a previously successful sample. */
	assert(!bq25970_check_configuration(&chip) && chip.sample_valid);
	for (int fault = 0; fault < 3; fault++) {
		assert(!bq25970_read_sample(&chip, &sample));
		if (fault == 0)
			fail_at = io_count + 1;
		else if (fault == 1)
			regs[0x00] ^= 1; /* Readback mismatch. */
		else
			regs[0x0b] |= BQ_WD_TIMEOUT_FLAG;
		int error = bq25970_check_configuration(&chip);
		assert(error == (fault == 0 ? -EIO : fault == 1 ? -EPERM : -ETIME));
		assert(!chip.sample_valid && chip.sample_error == error);
		assert(bq25970_get_property(&sw, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value) == error);
		assert(bq25970_get_property(&sw, POWER_SUPPLY_PROP_CURRENT_NOW, &value) == error);
		assert(!bq25970_get_property(&sw, POWER_SUPPLY_PROP_HEALTH, &value));
		assert(value.intval == (fault == 2 ? POWER_SUPPLY_HEALTH_UNSPEC_FAILURE :
			POWER_SUPPLY_HEALTH_UNKNOWN));
		fail_at = 0;
		if (fault == 1)
			regs[0x00] ^= 1;
		regs[0x0b] &= ~BQ_WD_TIMEOUT_FLAG;
	}
	assert(!bq25970_prepare(&chip));
	assert(!bq25970_read_sample(&chip, &sample));
	assert(!bq25970_get_property(&sw, POWER_SUPPLY_PROP_CURRENT_NOW, &value));
	chip.psy = &sw;before = changed;
	assert(!bq25970_stop(&chip));
	assert(changed == before + 1);
	assert(!bq25970_stop(&chip) && changed == before + 1);
	assert(bq25970_get_property(&sw, POWER_SUPPLY_PROP_CURRENT_NOW, &value) == -ENODATA);
	reset();regs[0x2b] = BIT(3);
	assert(umi_pps_prepare(&c) == -EPERM && (regs[0x2b] & BIT(3)));
	assert(!enable_count); /* The unknown VOUT bit is checked, never guessed. */
}
