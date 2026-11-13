// SPDX-License-Identifier: AGPL-3.0-only
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include "umi_charge_policy.h"
#define BIT(n) (1U << (n))
#define HZ 1000
#define time_before(a,b) ((long)((a)-(b))<0)
#define time_after_eq(a,b) ((long)((a)-(b))>=0)
#define min(a,b) ((a)<(b)?(a):(b))
#define USBIN_CMD_IL 0x340
#define USBIN_SUSPEND_BIT 1
enum power_supply_property { POWER_SUPPLY_PROP_USB_TYPE,
 POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, POWER_SUPPLY_PROP_ONLINE,
 POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW };
enum { POWER_SUPPLY_USB_TYPE_C, POWER_SUPPLY_USB_TYPE_PD, POWER_SUPPLY_USB_TYPE_PD_PPS };
union power_supply_propval { int intval; };
struct smb5_chip {
 void *pd, *regmap, *usb_in_v_chan;
 unsigned int base, thermal_drop_ua;
 int thermal_last_decic, power_stage, pd_voltage_uv, pd_current_ua, pd_limit_uv;
 bool thermal_sampled, thermal_recover, full_bypass, inhibit, cooling_state;
 bool pps_fault, pd_transition, enabled;
 unsigned long pd_deadline, thermal_recovery_jiffies;
};
static unsigned long jiffies;
static int props[5], input_uv, suspended, stopped, fsw, allowance;
static int fail_at, calls, pending, transitions, no_9v, sets;
static int fault(void) { return ++calls==fail_at ? -EIO : 0; }
static int power_supply_get_property(void *p, enum power_supply_property prop,
 union power_supply_propval *v) {
 int ret=fault();if(ret)return ret;v->intval=props[prop];return 0;
}
static int power_supply_set_property(void *p, enum power_supply_property prop,
 const union power_supply_propval *v) {
 int ret=fault();if(ret)return ret;
 assert(prop==POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT);
 assert(stopped && suspended && fsw==8 && allowance==8);sets++;
 if(pending)return -EAGAIN;
 props[prop]=v->intval;
 props[POWER_SUPPLY_PROP_VOLTAGE_NOW]=no_9v?5000000:v->intval;
 props[POWER_SUPPLY_PROP_CURRENT_NOW]=props[POWER_SUPPLY_PROP_VOLTAGE_NOW]==9000000?2000000:3000000;
 input_uv=props[POWER_SUPPLY_PROP_VOLTAGE_NOW];transitions++;return 0;
}
static int smb5_inhibit_with_fallback(struct smb5_chip *c,const char *why) {
 int ret=fault();if(!ret)stopped=1;return ret;
}
static int regmap_update_bits(void *m,unsigned int reg,unsigned int mask,unsigned int value) {
 int ret=fault();if(ret)return ret;assert(stopped);suspended=!!value;return 0;
}
static int regmap_write(void *m,unsigned int reg,unsigned int value) {
 int ret=fault();if(ret)return ret;
 if(reg==0x150)fsw=value;else {assert(reg==0x344);allowance=value;}return 0;
}
static int smb5_get_iio_chan(struct smb5_chip *c,void *chan,int *value) {
 int ret=fault();*value=input_uv;return ret;
}
/* DRIVER_UNDER_TEST */
static struct smb5_chip reset(void) {
 props[0]=POWER_SUPPLY_USB_TYPE_PD;props[1]=5000000;props[2]=1;
 props[3]=input_uv=5000000;props[4]=3000000;
 jiffies=0;fail_at=calls=pending=transitions=no_9v=sets=0;
 stopped=suspended=fsw=allowance=0;
 return (struct smb5_chip){.power_stage=1,.thermal_last_decic=300,.enabled=true};
}
int main(void) {
 struct smb5_chip c=reset();
 assert(!smb5_fixed_update(&c) && c.pd_voltage_uv==9000000 && c.pd_current_ua==2000000);
 assert(!c.pd_transition && !c.enabled && fsw==8 && allowance==2);
 assert(!smb5_fixed_update(&c) && transitions==1); /* Stable contract: no requests. */
 c.power_stage=2;
 assert(!smb5_fixed_update(&c) && c.pd_voltage_uv==5000000 && allowance==1 && fsw==15);
 c=reset();props[0]=POWER_SUPPLY_USB_TYPE_PD_PPS;
 assert(!smb5_fixed_update(&c) && c.pd_voltage_uv==5000000); /* Pump-compatible default. */
 c=reset();no_9v=1;
 assert(!smb5_fixed_update(&c) && c.pd_voltage_uv==5000000 && c.pd_current_ua==3000000);
 c=reset();pending=1;
 assert(smb5_fixed_update(&c)==-EAGAIN && c.pd_transition && !c.enabled && suspended);
 pending=0;assert(!smb5_fixed_update(&c) && c.pd_voltage_uv==9000000);
 c=reset();pending=1;assert(smb5_fixed_update(&c)==-EAGAIN);
 jiffies=10000;assert(smb5_fixed_update(&c)==-ETIMEDOUT && c.pps_fault && c.pd_transition);
 /* Every I/O failure keeps charging off; none can be reported as success. */
 for(int point=1;point<=13;point++) {
  c=reset();fail_at=point;int ret=smb5_fixed_update(&c);
  assert(ret==-EIO && (c.pps_fault || point==1));
  if(point>2)assert(!c.enabled);
 }
 c=reset();assert(!smb5_fixed_update(&c));props[3]=12000000;
 assert(smb5_fixed_update(&c)==-EPROTO && c.pps_fault);
 c=reset();assert(!smb5_fixed_update(&c));props[4]=3000000;
 assert(smb5_fixed_update(&c)==-EPROTO && c.pps_fault);
 c=reset();c.inhibit=true;assert(!smb5_fixed_update(&c) && c.pd_voltage_uv==5000000);
 c=reset();smb5_thermal_limit(&c,300);assert(!c.thermal_drop_ua);
 for(int t=310;t<=350;t+=10) { jiffies+=1000;smb5_thermal_limit(&c,t); }
 assert(c.thermal_drop_ua==500000);
 smb5_thermal_limit(&c,350);assert(c.thermal_drop_ua==500000);
 jiffies+=1000;smb5_thermal_limit(&c,340);assert(c.thermal_drop_ua==400000);
 for(int i=0;i<5;i++)smb5_thermal_limit(&c,340);
 assert(c.thermal_drop_ua==400000); /* IRQ bursts cannot accelerate recovery. */
 for(int i=0;i<5;i++){jiffies+=1000;smb5_thermal_limit(&c,340);}
 assert(!c.thermal_drop_ua);
 return 0;
}
