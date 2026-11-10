// SPDX-License-Identifier: AGPL-3.0-only
/* Exercise actual worker notifications with synthetic wired PD/pump inputs. */
static void test_notifications(void)
{
 union power_supply_propval value;
 int before;

 reset();c.psy=&sw;changed=0;
 tick();assert(changed==1 && c.state==PPS_WAIT_PLUG);
 for(int i=0;i<20;i++)tick();
 assert(changed==1 && !enable_count && !voltage_requests);
 /* Policy state stays WAIT_PLUG, but upstream online/type changes notify. */
 c.manual_stop=true;tick();before=changed;
 pd.prop[POWER_SUPPLY_PROP_ONLINE]=1;tick();assert(changed==before+1);
 before=changed;tick();assert(changed==before);
 pd.prop[POWER_SUPPLY_PROP_USB_TYPE]=POWER_SUPPLY_USB_TYPE_PD;
 tick();assert(changed==before+1 && c.state==PPS_WAIT_PLUG);
 before=changed;
 pd.prop[POWER_SUPPLY_PROP_ONLINE]=2;tick();assert(changed==before+1);
 /* Actual switching status can change without any policy transition. */
 before=changed;regs[0x0a]|=BIT(2);tick();assert(changed==before+1);
 before=changed;tick();assert(changed==before);
 regs[0x0a]&=~BIT(2);tick();assert(changed==before+1);
 before=changed;pd.prop[POWER_SUPPLY_PROP_ONLINE]=0;
 tick();assert(changed==before+1);before=changed;
 for(int i=0;i<20;i++)tick();
 assert(changed==before && !enable_count);
 /* A failed status read and its recovery notify, without consuming flags. */
 unsigned int latched=regs[0x11];
 for(int offset=1;offset<=3;offset++) {
  before=changed;fail_at=io_count+offset;
  assert(umi_pps_changed(&c));power_supply_changed(c.psy);
  fail_at=io_count+offset;
  assert(!umi_pps_changed(&c));
  fail_at=0;assert(umi_pps_changed(&c));power_supply_changed(c.psy);
  assert(changed==before+2 && regs[0x11]==latched);
  assert(!umi_pps_changed(&c));
 }
 /* Setter and poll share a snapshot; idempotent commands stay quiet. */
 reset();c.psy=&sw;changed=0;tick();
 value.intval=POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
 assert(!umi_pps_set_property(&sw,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&value));
 before=changed;
 assert(!umi_pps_set_property(&sw,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&value));
 assert(changed==before);
 tick();before=changed;tick();assert(changed==before);
 /* Reported health changes even when policy remains FAULT. */
 pd.prop[POWER_SUPPLY_PROP_ONLINE]=1;
 c.state=PPS_FAULT;c.last_error=-EOPNOTSUPP;tick();before=changed;
 c.last_error=-EIO;tick();assert(changed==before+1);
 before=changed;tick();assert(changed==before);
 c.suspended=true;tick();assert(changed==before);
 c.suspended=false;c.removing=true;tick();assert(changed==before);
 /* Exercise active -> fault -> failed-stop recovery notifications. */
 reset();c.psy=&sw;changed=0;run();before=changed;
 fail_stop=1;bms.prop[POWER_SUPPLY_PROP_TEMP]=451;tick();
 assert(changed==before+1 && c.stop_pending);before=changed;
 tick();assert(changed==before);
 fail_stop=0;tick();assert(changed==before+1 && !c.stop_pending);
 before=changed;tick();assert(changed==before);
}
