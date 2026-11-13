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
