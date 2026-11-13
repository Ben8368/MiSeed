// SPDX-License-Identifier: AGPL-3.0-only
/* Fault, deadline and cleanup cases for the actual PPS worker. */
static void test_fault_paths(void)
{
 /* A latched timeout blocks startup; the clean 30 s setting runs normally. */
 reset();regs[0x0b]=0x4b;begin();assert(c.state==PPS_FAULT&&!enable_count&&!wd_writes&&!(regs[0x0b]&BIT(3)));
 reset();regs[0x0b]=0x43;run();assert(regs[0x0b]==0x43&&wd_writes==1);
 for(int i=0;i<10;i++){clock_ms+=200;tick();assert(c.state==PPS_RUN && wd_writes==1);}
 assert(!umi_pps_stop(&c,PPS_OFF,0) && wd_writes==1);
 /* Current/voltage/temperature/SOC/protection/contract/ownership failures. */
 for(int fault=0;fault<26;fault++) {
  reset();run();adc(4,4105);int prior_wd_writes=wd_writes;
  switch(fault) {
   case 0: fail_sensor=1;break;
   case 1: bms.prop[POWER_SUPPLY_PROP_TEMP]=451;break;
   case 2: bms.prop[POWER_SUPPLY_PROP_TEMP]=150;break;
   case 3: bms.prop[POWER_SUPPLY_PROP_CAPACITY]=95;break;
   case 4: bms.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=4450000;break;
   case 5: bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=6001000;break;
   case 6: regs[0x10]=0x80;break;
   case 7: regs[0x11]=0x01;break;
   case 8: regs[0x0b]|=BIT(2);break; /* Wrong watchdog timeout encoding. */
   case 9: bad_readback=1;break;
   case 10: adc(0,3001);break;
   case 11: adc(4,4450);break;
   case 12: pd.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=9000000;break;
   case 13: suspended=0;break;
   case 14: regs[0x01]&=~0x80;break;
   case 15: regs[0x0c]&=~BIT(7);break;
   case 16: adc(0,-1);break; /* actual reverse BUS current still stops */
   case 17: regs[0x05]|=BIT(7);break;
   case 18: regs[0x0a]|=BIT(1);break;
   case 19: regs[0x0a]|=BIT(0);break;
   case 20: regs[0x2c]|=BIT(0);break;
   case 21: pd.prop[POWER_SUPPLY_PROP_CURRENT_MAX]=2000000;break;
   case 22: regs[0x2b]|=BIT(3);break;
   case 23: adc(4,3499);break;
   case 24: adc(1,c.request_mv+201);break;
   case 25: bms.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=3499000;break;
  }
  clock_ms+=200;tick();
  if(fault==3 || fault==4)
   assert(c.state==(fault==3 ? PPS_WAIT_PLUG : PPS_DONE) && !claim_owned && !(regs[0x0c]&BIT(7)));
  else
   assert(c.state==PPS_FAULT);
  assert(!(regs[0x0c]&BIT(7)) && !claim_owned && wd_writes==prior_wd_writes);
 }
 /* No enable after failed negotiation, missing APDO, or exclusive claim. */
 reset();pd.prop[POWER_SUPPLY_PROP_USB_TYPE]=POWER_SUPPLY_USB_TYPE_PD;
 fail_pps_unsupported=1;begin();
 assert(c.state==PPS_FAULT && !claim_owned && !c.pps && !fixed_count &&
        !pps_activations && !enable_count);
 for(int f=0;f<5;f++) {
  reset();
  if(f==0) fail_request=1;
  if(f==1) fail_caps=1;
  if(f==2) stale_contract=1;
  if(f==3) fail_claim=1;
  if(f==4) pd.prop[POWER_SUPPLY_PROP_CURRENT_MAX]=1900000;
  begin();
  if(f==3)
   assert(c.state==PPS_WAIT_PLUG && !claim_owned && !enable_count);
  else
   assert(c.state==PPS_FAULT && !enable_count);
 }
 reset();expired_negotiation=1;begin();clock_ms+=200;tick();
 assert((c.state==PPS_DONE || c.state==PPS_FAULT) && !enable_count && !claim_owned);
 /* Slow protection setup must not activate PPS after its operation window. */
 reset();delayed_reg=0x02;read_delay_ms=60001;begin();
 assert(c.state==PPS_FAULT && c.last_error==-ETIME && !pps_activations && !enable_count);
 /* A blocking read may cross an operation or startup window after first check. */
 reset();begin();clock_ms=c.start_deadline-1;delayed_reg=0x0d;read_delay_ms=2;
 int pre_enable_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && c.last_error==-ETIMEDOUT && !enable_count && !claim_owned);
 assert(wd_writes==pre_enable_wd_writes);
 reset();begin();clock_ms=c.start_deadline-1;delayed_reg=0x0b;read_delay_ms=2;
 pre_enable_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && !enable_count && wd_writes==pre_enable_wd_writes);
 reset();run();delayed_reg=0x0d;read_delay_ms=10001;
 int old_voltage=voltage_requests,old_current=current_requests,old_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && c.last_error==-ETIME && !claim_owned);
 assert(voltage_requests==old_voltage && current_requests==old_current && wd_writes==old_wd_writes);
 /* An expired voltage request must not be followed by a current request. */
 reset();run();clock_ms=c.last_pps_request_ms+UMI_PPS_REFRESH_MS;request_delay_ms=10001;
 old_current=current_requests;old_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && current_requests==old_current && wd_writes==old_wd_writes);
 /* Protection verification may itself consume the remaining control window. */
 reset();run();clock_ms=c.deadline-1;delayed_reg=0x02;read_delay_ms=2;
 old_wd_writes=wd_writes;assert(umi_pps_validate_cycle(&c)==-ETIME && wd_writes==old_wd_writes);
 reset();run();clock_ms=c.last_pps_request_ms+UMI_PPS_REFRESH_MS;hot_during_request=1;int pre_fault_wd_writes=wd_writes;clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && wd_writes==pre_fault_wd_writes && !claim_owned);
 /* Missing, implausible or stale external telemetry must stop without watchdog writes. */
 for(int i=1;i<=5;i++) {
  reset();run();thermal_fail=i;int previous=wd_writes;tick();
  assert(c.state==PPS_FAULT && !claim_owned && wd_writes==previous);
 }
 reset();thermal_delay_ms=41;begin();assert(c.state==PPS_WAIT_PLUG && !enable_count);
 reset();run();thermal_mc[1]=125000;pre_fault_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && wd_writes==pre_fault_wd_writes);
 /* Proactive thermal reduction recovers after cooling. */
 reset();run();bms.prop[POWER_SUPPLY_PROP_TEMP]=400;
 for(int i=0;i<5;i++)thermal_mc[i]=40000;
 tick();assert(c.state==PPS_RUN && c.thermal.level==8 && c.request_ma==1200);
 bms.prop[POWER_SUPPLY_PROP_TEMP]=300;for(int i=0;i<5;i++)thermal_mc[i]=30000;
 clock_ms+=1000;tick();assert(c.state==PPS_RUN && c.request_ma==1250);
 /* The next thermal band can remain PPS at a 1 A request. */
 reset();run();bms.prop[POWER_SUPPLY_PROP_TEMP]=430;
 for(int i=0;i<5;i++)thermal_mc[i]=43000;
 adc(0,800);
 tick();assert(c.state==PPS_RUN && c.request_ma==1000 && claim_owned && !fixed_count);
 /* Failed stop retains ownership, does not write watchdog, retries both mechanisms. */
 reset();run();fail_stop=1;int prior_wd_writes=wd_writes;
 assert(umi_pps_stop(&c,PPS_OFF,0)==-EIO && c.stop_pending && claim_owned);
 assert(wd_writes==prior_wd_writes);fail_stop=0;tick();assert(!c.stop_pending && !claim_owned);
 reset();run();fail_fixed=1;
 assert(umi_pps_stop(&c,PPS_OFF,0)==-EIO && claim_owned && !(regs[0x0c]&BIT(7)));
 fail_fixed=0;tick();assert(!claim_owned && !c.stop_pending);
 /* Any individual initialization / negotiation I/O failure must inhibit. */
 reset();begin();int setup_io=io_count;
 for(int n=1;n<=setup_io;n++) {
  reset();fail_at=n;begin();assert(!enable_count);
  fail_at=0;umi_pps_stop(&c,PPS_OFF,0);assert(!claim_owned);
 }
 /* Any individual running-cycle I/O failure must stop, without another enable. */
 reset();run();io_count=0;clock_ms+=200;tick();int running_io=io_count;
 for(int n=1;n<=running_io;n++) {
  reset();run();io_count=0;fail_at=n;clock_ms+=200;tick();
  assert(c.state==PPS_FAULT && enable_count==1);
  fail_at=0;umi_pps_stop(&c,PPS_OFF,0);assert(!claim_owned);
 }
}
