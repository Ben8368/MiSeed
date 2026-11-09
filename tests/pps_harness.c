// SPDX-License-Identifier: AGPL-3.0-only
/* Synthetic board values below are test fixtures, NOT Umi calibration data. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
#define BIT(n) (1U << (n))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
struct device { int unused; };
static void *drvdata;
static void *dev_get_drvdata(struct device *dev) { return drvdata; }
struct mutex { int unused; };
struct regmap { int unused; };
struct iio_channel { int index; };
static struct iio_channel thermal_channels[5];
static int thermal_mc[5], thermal_fail, thermal_delay_ms;
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
#define to_delayed_work(p) container_of(p, struct delayed_work, work)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
static int info_count, scheduled, check_operating_power;
#define dev_info(...) (info_count++)
#define dev_err_probe(d,e,...) (e)
#define system_wq NULL
#define msecs_to_jiffies(v) (v)
#define mod_delayed_work(q,w,t) (scheduled++)
static u64 clock_ms;
static int retry_sleeps;
static void usleep_range(unsigned int low, unsigned int high) {
 assert(low==20000 && high==25000);clock_ms+=low/1000;retry_sleeps++;
}
static u64 ktime_get_boottime(void) { return clock_ms; }
static u64 ktime_to_ms(u64 v) { return v; }
enum power_supply_property { POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_PRESENT,
 POWER_SUPPLY_PROP_TEMP, POWER_SUPPLY_PROP_CAPACITY, POWER_SUPPLY_PROP_HEALTH,
 POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW, POWER_SUPPLY_PROP_USB_TYPE,
 POWER_SUPPLY_PROP_VOLTAGE_MIN, POWER_SUPPLY_PROP_VOLTAGE_MAX, POWER_SUPPLY_PROP_CURRENT_MAX,
 PROP_COUNT };
#define POWER_SUPPLY_HEALTH_GOOD 1
#define POWER_SUPPLY_USB_TYPE_PD 6
#define POWER_SUPPLY_USB_TYPE_PD_PPS 7
union power_supply_propval { int intval; };
struct power_supply { int id; int prop[PROP_COUNT]; };
static struct power_supply pd, bms, sw;
static unsigned int regs[0x30];
static int io_count, fail_at, enable_count, fixed_count, release_count, wd_writes;
static int fail_stop, fail_fixed, fail_claim, fail_sensor, fail_request, fail_caps;
static int fail_pps_unsupported;
static int busy_count, busy_delay_ms;
static enum power_supply_property busy_prop;
static int claim_owned, suspended, stale_contract, expired_negotiation, bad_readback, hot_during_request;
static unsigned int delayed_reg;
static u64 read_delay_ms, request_delay_ms;
static int voltage_requests, current_requests, pps_activations, pump_during_request, vbus_request_drop, bus_fault_during_request;
static const char *missing_property;
static int io_fail(void) { return ++io_count == fail_at ? -EIO : 0; }
static int iio_read_channel_processed(struct iio_channel *chan, int *v) {
 int ret=io_fail();if(ret)return ret;
 if(thermal_fail && chan->index==thermal_fail-1)return -EIO;
 clock_ms+=thermal_delay_ms;*v=thermal_mc[chan->index];return 0;
}
static void adc(int channel, int value) {
 regs[0x16 + channel*2] = ((unsigned int)value >> 8) & 255;
 regs[0x17 + channel*2] = value & 255;
}
static int regmap_read(struct regmap *map, unsigned int r, unsigned int *v) {
 int ret = io_fail(); if (ret) return ret;
 if (read_delay_ms && r == delayed_reg) { clock_ms += read_delay_ms; read_delay_ms=0; }
 *v = regs[r]; if (bad_readback && r == 0x02) *v ^= 0x80;
 return 0;
}
static int regmap_write(struct regmap *map, unsigned int r, unsigned int v) {
 int ret = io_fail(); if (ret) return ret;
 if (r == 0x0b) wd_writes++;
 if (r == 0x0c) {
  if (fail_stop && !(v & BIT(7))) return -EIO;
  if (v & BIT(7)) {
   assert(claim_owned && suspended && pd.prop[POWER_SUPPLY_PROP_ONLINE] == 2);
   enable_count++; regs[0x0a] |= BIT(2);
   adc(0,1000); bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW] = -2000000;
  } else {
   regs[0x0a] &= ~BIT(2); adc(0,0);
   bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW] = 0;
  }
 }
 regs[r] = v; return 0;
}
static int regmap_update_bits(struct regmap *map, unsigned int r, unsigned int mask, unsigned int v) {
 return regmap_write(map, r, (regs[r] & ~mask) | v);
}
static int regmap_bulk_read(struct regmap *map, unsigned int r, u8 *v, int n) {
 int ret = io_fail(); if (ret) return ret;
 for (int i=0; i<n; i++) v[i]=(u8)regs[r+i];
 return 0;
}
static int power_supply_get_property(struct power_supply *p, enum power_supply_property prop,
                                    union power_supply_propval *v) {
 int ret=io_fail(); if(ret) return ret;
 if (fail_sensor && p == &bms) return -EIO;
 if (fail_caps && prop == POWER_SUPPLY_PROP_VOLTAGE_MAX) return -EIO;
 v->intval=p->prop[prop]; return 0;
}
static int power_supply_set_property(struct power_supply *p, enum power_supply_property prop,
                                    const union power_supply_propval *v) {
 int ret=io_fail(); if(ret) return ret;
 assert(p == &pd);
 if (check_operating_power && ((prop==POWER_SUPPLY_PROP_CURRENT_NOW && (v->intval/1000)*(p->prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]/1000)/1000 < @OPERATING_SINK_MW@) ||
     (prop==POWER_SUPPLY_PROP_VOLTAGE_NOW && (v->intval/1000)*(p->prop[POWER_SUPPLY_PROP_CURRENT_NOW]/1000)/1000 < @OPERATING_SINK_MW@))) return -EINVAL;
 if (prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) voltage_requests++;
 if (prop == POWER_SUPPLY_PROP_CURRENT_NOW) current_requests++;
 if (busy_count && prop == busy_prop) {
  busy_count--;clock_ms+=busy_delay_ms;return -EAGAIN;
 }
 if (prop == POWER_SUPPLY_PROP_ONLINE && v->intval == 1) {
  fixed_count++; if(fail_fixed) return -EIO;
  if(!stale_contract) {
   p->prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=5000000;
   p->prop[POWER_SUPPLY_PROP_CURRENT_NOW]=500000;
  }
 }
 if (prop == POWER_SUPPLY_PROP_ONLINE && v->intval == 2) {
  if (fail_pps_unsupported) return -EOPNOTSUPP;
  assert(claim_owned && suspended);pps_activations++;
 }
 if (fail_request && prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) return -EIO;
 if (expired_negotiation && prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) clock_ms += 601000;
 if (request_delay_ms && prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) {
  clock_ms += request_delay_ms; request_delay_ms=0;
 }
 if (hot_during_request && prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) bms.prop[POWER_SUPPLY_PROP_TEMP]=451;
 if (pump_during_request && prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) adc(4,pump_during_request);
 if (!stale_contract) p->prop[prop]=v->intval;
 if (prop == POWER_SUPPLY_PROP_VOLTAGE_NOW) {
  adc(1,v->intval/1000-vbus_request_drop);
  regs[0x0a] |= bus_fault_during_request;
 }
 return 0;
}
int umi_smb5_claim(struct power_supply *p) {
 if (fail_claim) return -EBUSY;
 claim_owned=1; suspended=1; return 0;
}
int umi_smb5_check(struct power_supply *p) { return claim_owned && suspended ? 4450000 : -EPERM; }
int umi_smb5_release(struct power_supply *p) {
 assert(!(regs[0x0c] & BIT(7))); assert(pd.prop[POWER_SUPPLY_PROP_ONLINE] != 2);
 release_count++; suspended=0; claim_owned=0; return 0;
}
static int device_property_read_u32(struct device *d, const char *key, u32 *v) {
 static const struct { const char *key; u32 value; } values[] = {
  {"ti,sense-resistor-micro-ohms",2000}, {"ti,tsbat-threshold-code",0x15},
  {"ti,tsbus-threshold-code",0x15}, {"ti,die-alarm-decicelsius",1000},
  {"miseed,die-max-decicelsius",800}, {"ti,bat-ovp-millivolt",4550},
  {"ti,bat-ovp-alarm-millivolt",4525}, {"ti,bat-ocp-milliamp",8000},
  {"ti,bat-ocp-alarm-milliamp",7500}, {"ti,bus-ovp-millivolt",12000},
  {"ti,bus-ovp-alarm-millivolt",11000}, {"ti,bus-ocp-milliamp",3750},
  {"ti,bus-ocp-alarm-milliamp",3500}, {"ti,ac-ovp-volt",14}
  ,{"miseed,thermal-board-compensation-millicelsius",0}
 };
 if(missing_property && !strcmp(key,missing_property)) return -EINVAL;
 for(unsigned int i=0;i<sizeof(values)/sizeof(values[0]);i++)
  if(!strcmp(key,values[i].key)){*v=values[i].value;return 0;}
 return -EINVAL;
}

/* MISEED_DRIVER_UNDER_TEST */

static struct umi_pps c;
static void reset(void) {
 drvdata=&c;
 info_count=scheduled=check_operating_power=0;
 memset(&c,0,sizeof(c));memset(regs,0,sizeof(regs));
 memset(&pd,0,sizeof(pd));memset(&bms,0,sizeof(bms));memset(&sw,0,sizeof(sw));
 c.pd=&pd;c.bms=&bms;c.sw=&sw;
 clock_ms=100; io_count=fail_at=enable_count=fixed_count=release_count=wd_writes=0;
 fail_stop=fail_fixed=fail_claim=fail_sensor=fail_request=fail_caps=0;
 fail_pps_unsupported=0;
 busy_count=busy_delay_ms=retry_sleeps=0;busy_prop=POWER_SUPPLY_PROP_ONLINE;
 claim_owned=suspended=stale_contract=expired_negotiation=bad_readback=0;
 missing_property=NULL;hot_during_request=pump_during_request=0;
 vbus_request_drop=bus_fault_during_request=0;
 delayed_reg=0;read_delay_ms=request_delay_ms=0;voltage_requests=current_requests=pps_activations=0;
 thermal_fail=thermal_delay_ms=0;
 for(int i=0;i<5;i++){thermal_channels[i].index=i;thermal_mc[i]=30000;c.thermal_channels[i]=&thermal_channels[i];}
 pd.prop[POWER_SUPPLY_PROP_USB_TYPE]=POWER_SUPPLY_USB_TYPE_PD_PPS;
 pd.prop[POWER_SUPPLY_PROP_VOLTAGE_MIN]=3300000;
 pd.prop[POWER_SUPPLY_PROP_VOLTAGE_MAX]=11000000;
 pd.prop[POWER_SUPPLY_PROP_CURRENT_MAX]=3000000;
 pd.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=5000000;
 pd.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=500000;
 bms.prop[POWER_SUPPLY_PROP_PRESENT]=1;bms.prop[POWER_SUPPLY_PROP_TEMP]=300;
 bms.prop[POWER_SUPPLY_PROP_CAPACITY]=50;bms.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=4000000;
 sw.prop[POWER_SUPPLY_PROP_HEALTH]=POWER_SUPPLY_HEALTH_GOOD;
 regs[0x0d]=6;adc(4,4000);adc(8,60);
 assert(!umi_pps_parse(&c));assert(c.nprotection==19);
 c.state=PPS_WAIT_PLUG;
}
static void tick(void) { umi_pps_work(&c.work.work); }
static void begin(void) {
	pd.prop[POWER_SUPPLY_PROP_ONLINE]=1;
	tick();
}
static void run(void) {
 begin(); assert(c.state==PPS_SETTLE && !enable_count);
 clock_ms+=200;tick(); assert(c.state==PPS_RUN && enable_count==1);
}
int main(void) {
 reset();run();assert(!umi_pps_suspend(NULL) && c.suspended && c.state==PPS_OFF);
 int previous_enable=enable_count;tick();assert(enable_count==previous_enable);
 assert(!umi_pps_resume(NULL) && !c.suspended && c.state==PPS_WAIT_PLUG);
 tick();assert(c.state==PPS_SETTLE);
 reset();c.manual_stop=true;c.state=PPS_OFF;pd.prop[POWER_SUPPLY_PROP_ONLINE]=1;
 assert(!umi_pps_suspend(NULL) && !umi_pps_resume(NULL) && c.manual_stop && c.state==PPS_OFF);
 tick();assert(scheduled && !enable_count);
 pd.prop[POWER_SUPPLY_PROP_ONLINE]=0;tick();assert(!c.manual_stop && c.state==PPS_WAIT_PLUG);
 reset();c.state=PPS_FAULT;c.last_error=-EIO;
 assert(!umi_pps_suspend(NULL) && !umi_pps_resume(NULL) && c.state==PPS_FAULT);
 reset();c.state=PPS_WAIT_PLUG;pd.prop[POWER_SUPPLY_PROP_ONLINE]=1;fail_sensor=1;tick();int first_error=info_count;
 tick();assert(first_error==1 && info_count==first_error && !claim_owned);
 reset();run();check_operating_power=1;c.request_mv=7300;pd.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=7300000;adc(1,7300);
 bms.prop[POWER_SUPPLY_PROP_TEMP]=400;for(int i=0;i<5;i++)thermal_mc[i]=40000;
 tick();assert(c.state==PPS_RUN && c.request_ma==2050);
 struct umi_pps_thermal thermal={0};int temperatures[6]={30000,30000,30000,30000,30000,30000};
 assert(umi_pps_thermal_level(803,1)==8 && umi_pps_thermal_level(803,0)==3);
 assert(umi_pps_thermal_level(803,-1)==8 && umi_pps_thermal_level(1500,-1)==15);
 assert(umi_pps_thermal_level(1615,-1)<0 && umi_pps_thermal_level(-1,0)<0);
 assert(!umi_pps_thermal_update(&thermal,temperatures,-1,0));
 assert(thermal.virtual_mc==32561 && thermal.level==0 && thermal.max_bat_ma==6000);
 /* At 40C inputs the observed weighted model is 41.901C, step 1. */
 for(int i=0;i<6;i++)temperatures[i]=40000;
 assert(!umi_pps_thermal_update(&thermal,temperatures,-1,0));
 assert(thermal.step==1 && thermal.level==8 && thermal.max_bat_ma==4100);
 for(int i=0;i<6;i++)temperatures[i]=38000;
 assert(!umi_pps_thermal_update(&thermal,temperatures,-1,0) && thermal.step==1);
 for(int i=0;i<6;i++)temperatures[i]=35000;
 assert(!umi_pps_thermal_update(&thermal,temperatures,-1,0) && thermal.step==0);
 temperatures[2]=125000;assert(umi_pps_thermal_update(&thermal,temperatures,-1,0)<0);
 temperatures[2]=35000;assert(umi_pps_thermal_update(&thermal,temperatures,-1,1)<0);
 struct umi_pps_caps caps={3300,11000,3000};
 assert(umi_pps_target(&caps,4000)==8400);
 assert(umi_pps_target(&caps,3499)<0);
 assert(umi_pps_target(&caps,INT_MAX)<0);
 assert(umi_pps_current(&caps,3000)==3000);
 caps.max_ma=2999;assert(umi_pps_current(&caps,3000)==2950);
 caps.max_ma=1999;assert(umi_pps_current(&caps,3000)<0);
 assert(umi_pps_encode(4550,3475,25,63)==43);
 assert(umi_pps_encode(4551,3475,25,63)<0);
 assert(umi_pps_encode(INT_MAX,3475,25,63)<0);
	reset();missing_property="ti,sense-resistor-micro-ohms";c.nprotection=0;
	assert(!umi_pps_parse(&c) && c.nprotection==19 && !enable_count);
 reset();pd.prop[POWER_SUPPLY_PROP_ONLINE]=1;tick();
 assert(c.state==PPS_SETTLE && claim_owned && !enable_count);
 reset();run();clock_ms+=200;tick();assert(c.state==PPS_RUN);
 pd.prop[POWER_SUPPLY_PROP_ONLINE]=0;tick();
 assert(c.state==PPS_WAIT_PLUG && !claim_owned && !(regs[0x0c]&BIT(7)));
 /* A source AMS / discarded transmission is transient; unrelated I/O errors
  * are still fatal. Reproduce the actual 3A activation without a duplicate AMS.
  */
 for(int prop=0;prop<3;prop++) {
  reset();busy_prop=prop==0 ? POWER_SUPPLY_PROP_ONLINE :
   prop==1 ? POWER_SUPPLY_PROP_VOLTAGE_NOW : POWER_SUPPLY_PROP_CURRENT_NOW;
  busy_count=2;run();assert(retry_sleeps==2 && !busy_count);
 }
 reset();pd.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=3000000;run();
 assert(current_requests==0);
 clock_ms+=200;tick();assert(c.state==PPS_RUN && current_requests==0 && voltage_requests>=2);
 /* Keep the control loop fast, but avoid repeated identical APDO requests.
  * Refresh at eight seconds while any voltage/current change remains immediate.
  */
 reset();run();bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=-5850000;adc(0,2850);
 int stable_requests=voltage_requests;clock_ms+=200;tick();
 assert(c.state==PPS_RUN && voltage_requests==stable_requests);
 clock_ms=c.last_pps_request_ms+UMI_PPS_REFRESH_MS;tick();
 assert(c.state==PPS_RUN && voltage_requests==stable_requests+1);
 reset();busy_prop=POWER_SUPPLY_PROP_VOLTAGE_NOW;busy_count=100;begin();
 assert(c.state==PPS_FAULT && !enable_count && !claim_owned && voltage_requests==4 && retry_sleeps==3);
 reset();busy_prop=POWER_SUPPLY_PROP_VOLTAGE_NOW;busy_count=100;busy_delay_ms=10000;begin();
 assert(c.state==PPS_FAULT && c.last_error==-ETIME && voltage_requests==1 && !retry_sleeps && !enable_count);
 reset();run();busy_prop=POWER_SUPPLY_PROP_VOLTAGE_NOW;busy_count=2;
 clock_ms+=200;tick();assert(c.state==PPS_RUN && retry_sleeps==2);
 /* PS_RDY may precede a fresh pump ADC sample. Wait without enabling or
  * requesting more voltage; absolute faults still stop immediately.
  */
 reset();begin();adc(1,c.request_mv-282);int old_requests=voltage_requests;
 clock_ms+=200;tick();assert(c.state==PPS_SETTLE && !enable_count && voltage_requests==old_requests);
 adc(1,c.request_mv);clock_ms+=200;tick();assert(c.state==PPS_RUN && enable_count==1);
 reset();begin();adc(1,c.request_mv-282);clock_ms=c.start_deadline;tick();
 assert(c.state==PPS_FAULT && c.last_error==-ETIMEDOUT && !enable_count && !claim_owned);
 reset();begin();adc(1,c.request_mv-282);regs[0x10]=0x80;clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && !enable_count && !claim_owned);
 reset();begin();adc(1,c.request_mv+201);clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && !enable_count && !claim_owned);
 /* No-load agreement remains exact at the 200/201mV boundary. */
 for(int drop=200;drop<=201;drop++) {
  reset();begin();adc(1,c.request_mv-drop);old_requests=voltage_requests;
  clock_ms+=200;tick();
  assert(c.state==(drop==200 ? PPS_RUN : PPS_SETTLE));
  assert(enable_count==(drop==200) && voltage_requests==old_requests);
 }
 /* L3: a valid 8880mV contract with 8676mV at the loaded pump, +1796mA
  * net battery charge and no bus fault used to abort just after a request.
  * Exercise the real worker across both pre- and post-request sampling.
  */
 for(int drop_index=0;drop_index<4;drop_index++) {
  const int drops[]={200,201,204,282};
  reset();run();c.request_mv=8860;
  pd.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=8860000;
  bms.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=4271000;
  bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=-1796000;
  adc(4,4285);adc(0,1199);adc(1,8680);
  vbus_request_drop=drops[drop_index];
  clock_ms+=200;tick();
  assert(c.state==PPS_RUN && claim_owned && c.request_mv==8880);
  assert(pd.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]==8880000);
  for(int cycle=0;cycle<3;cycle++) {
   clock_ms+=200;tick();assert(c.state==PPS_RUN && claim_owned);
  }
 }
 /* A loaded voltage drop never exempts bus faults, stopped switching,
  * absolute voltage/current/temperature limits or a mismatched contract.
  */
 for(int fault=0;fault<10;fault++) {
  reset();run();adc(1,c.request_mv-204);vbus_request_drop=204;
  switch(fault) {
   case 0: regs[0x0a]|=BIT(0);break;
   case 1: regs[0x0a]|=BIT(1);break;
   case 2: regs[0x0a]&=~BIT(2);break;
   case 3: adc(1,6999);break;
   case 4: adc(1,9501);break;
   case 5: adc(0,3001);break;
   case 6: adc(4,4450);break;
   case 7: bms.prop[POWER_SUPPLY_PROP_TEMP]=451;break;
   case 8: pd.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]+=20000;break;
   case 9: regs[0x10]=0x80;break;
  }
  clock_ms+=200;tick();
  assert(c.state==PPS_FAULT && !claim_owned && !(regs[0x0c]&BIT(7)));
 }
 /* Faults arriving during the negotiated request still fail the fresh read. */
 for(int fault=0;fault<4;fault++) {
  reset();run();vbus_request_drop=204;
  if(fault<2)bus_fault_during_request=BIT(fault);
  if(fault==2)vbus_request_drop=c.request_mv+UMI_PPS_STEP_MV-6999;
  if(fault==3)vbus_request_drop=-201;
  clock_ms+=200;tick();
  assert(c.state==PPS_FAULT && !claim_owned && !(regs[0x0c]&BIT(7)));
 }
 /* Actual L3: first BMS/pump voltages differed by 54mV after SMB5 pause.
  * Both polarities wait with the pump OFF, then require the original 50mV.
  */
 for(int delta=-54;delta<=54;delta+=108) {
  reset();begin();adc(4,4000+delta);old_requests=voltage_requests;
  clock_ms+=200;tick();assert(c.state==PPS_SETTLE && !enable_count && voltage_requests==old_requests);
  adc(4,4000);clock_ms+=200;tick();assert(c.state==PPS_RUN && enable_count==1);
 }
 reset();begin();adc(4,3946);clock_ms=c.start_deadline;tick();
 assert(c.state==PPS_FAULT && c.last_error==-ETIMEDOUT && !enable_count && !claim_owned);
 reset();begin();adc(4,3946);adc(0,-1);clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && !enable_count && !claim_owned);
 reset();begin();adc(4,4450);clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && !enable_count && !claim_owned);
 reset();begin();adc(4,3946);regs[0x10]=0x80;clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && !enable_count && !claim_owned);
 /* Once enabled, the retained L3 offsets (+105/+110mV) and both signs
  * remain valid only within each independent absolute limit. */
 const int offsets[]={-110,-105,-54,54,105,110};
 for(unsigned int i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
  reset();run();adc(4,4000+offsets[i]);
  for(int cycle=0;cycle<3;cycle++){clock_ms+=200;tick();assert(c.state==PPS_RUN && claim_owned);}
 }
 /* Startup agreement still includes exactly +/-50mV, not +/-51mV. */
 for(int sign=-1;sign<=1;sign+=2) {
  reset();begin();adc(4,4000+sign*51);clock_ms+=200;tick();
  assert(c.state==PPS_SETTLE && !enable_count);
  adc(4,4000+sign*50);clock_ms+=200;tick();assert(c.state==PPS_RUN);
 }
 /* Either higher VBAT bounds the voltage loop and taper. */
 for(int high_bms=0;high_bms<2;high_bms++) {
  reset();run();int previous_mv=c.request_mv;
  bms.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=high_bms ? 4430000 : 4310000;
  adc(4,high_bms ? 4310 : 4430);clock_ms+=200;tick();
  assert(c.state==PPS_RUN && c.request_mv==previous_mv-UMI_PPS_STEP_MV);
  assert(c.taper_start);clock_ms+=UMI_PPS_TAPER_MS;tick();
  assert(c.state==PPS_DONE && !claim_owned && !(regs[0x0c]&BIT(7)));
  reset();run();previous_mv=c.request_mv;
  bms.prop[POWER_SUPPLY_PROP_VOLTAGE_NOW]=high_bms ? 4425000 : 4310000;
  adc(4,high_bms ? 4310 : 4425);clock_ms+=200;tick();
  assert(c.state==PPS_RUN && c.request_mv==previous_mv);
 }
 /* Fresh pump telemetry is revalidated after a sleeping PD request. */
 for(int invalid=0;invalid<2;invalid++) {
  reset();run();pump_during_request=invalid ? 4450 : 4105;
  clock_ms+=200;tick();
  assert(c.state==(invalid ? PPS_FAULT : PPS_RUN));
  assert(claim_owned==!invalid);
 }
 /* Match LOS ADC selection and BMS current semantics. An unused IBAT ADC
  * value cannot become a guessed sense-resistor safety threshold. */
 reset();run();assert(regs[0x15]==0x2f);adc(5,-7);
 bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=386000;clock_ms+=200;tick();
 assert(c.state==PPS_RUN && claim_owned);
	reset();run();clock_ms+=600000;tick();assert(c.state==PPS_RUN);
	bms.prop[POWER_SUPPLY_PROP_CAPACITY]=95;clock_ms+=200;tick();
	assert(c.state==PPS_DONE && release_count==1 && !claim_owned);
 /* Match LOS: disable only bit 2, preserve frequency/timing and do not feed.
  * A historical timeout flag is not an active watchdog with DIS set.
  */
 reset();regs[0x0b]=0x4b;run();assert(regs[0x0b]==0x4f && wd_writes==1);
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
   case 5: bms.prop[POWER_SUPPLY_PROP_CURRENT_NOW]=-6001000;break;
   case 6: regs[0x10]=0x80;break;
   case 7: regs[0x11]=0x01;break;
   case 8: regs[0x0b]&=~BIT(2);break;
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
   assert(c.state==PPS_DONE && !claim_owned && !(regs[0x0c]&BIT(7)));
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
 reset();run();request_delay_ms=10001;
 old_current=current_requests;old_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && current_requests==old_current && wd_writes==old_wd_writes);
 /* Protection verification may itself consume the remaining control window. */
 reset();run();clock_ms=c.deadline-1;delayed_reg=0x02;read_delay_ms=2;
 old_wd_writes=wd_writes;assert(umi_pps_validate_cycle(&c)==-ETIME && wd_writes==old_wd_writes);
 reset();run();hot_during_request=1;int pre_fault_wd_writes=wd_writes;clock_ms+=200;tick();
 assert(c.state==PPS_FAULT && wd_writes==pre_fault_wd_writes && !claim_owned);
 /* Missing, implausible or stale external telemetry must stop without watchdog writes. */
 for(int i=1;i<=5;i++) {
  reset();run();thermal_fail=i;int previous=wd_writes;tick();
  assert(c.state==PPS_FAULT && !claim_owned && wd_writes==previous);
 }
 reset();thermal_delay_ms=41;begin();assert(c.state==PPS_WAIT_PLUG && !enable_count);
 reset();run();thermal_mc[1]=125000;pre_fault_wd_writes=wd_writes;tick();
 assert(c.state==PPS_FAULT && wd_writes==pre_fault_wd_writes);
 /* Thermal reduction is applied to PD current, and cannot auto-increase. */
 reset();run();bms.prop[POWER_SUPPLY_PROP_TEMP]=400;
 for(int i=0;i<5;i++)thermal_mc[i]=40000;
 tick();assert(c.state==PPS_RUN && c.thermal.level==8 && c.request_ma==2050);
 bms.prop[POWER_SUPPLY_PROP_TEMP]=300;for(int i=0;i<5;i++)thermal_mc[i]=30000;
 tick();assert(c.state==PPS_RUN && c.request_ma==2050);
 /* A lower LOS FCC than this PPS profile can support ends the session. */
 reset();run();bms.prop[POWER_SUPPLY_PROP_TEMP]=430;
 for(int i=0;i<5;i++)thermal_mc[i]=43000;
 tick();assert(c.state==PPS_FAULT && !claim_owned);
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
 puts("PPS real-driver fault injection passed");return 0;
}
