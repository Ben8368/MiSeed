// SPDX-License-Identifier: AGPL-3.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
typedef uint32_t u32;
#define PDO_MAX_OBJECTS 7
#define PDO_TYPE_FIXED 0
#define PDO_TYPE_VAR 2
#define PDO_TYPE_APDO 3
#define pdo_type(p) ((p)>>30)
#define pdo_fixed_voltage(p) (((p)>>10 & 1023)*50)
#define max(a,b) ((a)>(b)?(a):(b))
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
enum { SNK_READY, SNK_NEGOTIATE_CAPABILITIES, INVALID_STATE, TYPEC_SINK, TYPEC_SOURCE,
 POWER_NEGOTIATION };
struct usb_power_delivery { int id; };
struct pd_data { struct { u32 pdo[7]; } source_desc,sink_desc;
 int operating_snk_mw;void *sink_cap;struct usb_power_delivery *pd; };
struct tcpm_port { int lock,state,pwr_role,pd_count,nr_snk_pdo,operating_snk_mw,upcoming_state;
 unsigned int psy_voltage_limit_mv; int supply_voltage;
 bool pd_capable,psy_selected_profile,update_sink_caps;
 struct { bool active; } pps_data;
 struct pd_data **pd_list;struct usb_power_delivery *selected_pd;
 u32 snk_pdo[7];void *port_sink_caps,*typec_port; };
union power_supply_propval { int intval; };
static int busy, starts;
static int tcpm_ams_start(struct tcpm_port *p,int ams) { starts++;return busy?-EAGAIN:0; }
static void typec_port_set_usb_power_delivery(void *p,void *pd) { }
/* DRIVER_UNDER_TEST */
int main(void) {
 struct usb_power_delivery ids[2]={{0},{1}};
 struct pd_data data[2]={
 {.sink_desc={{(5000/50<<10)|300,3U<<30}},.pd=&ids[0],.operating_snk_mw=7000},
 {.sink_desc={{(5000/50<<10)|300,(9000/50<<10)|200,3U<<30}},.pd=&ids[1],.operating_snk_mw=7000}};
 struct pd_data *list[]={&data[0],&data[1]};
 struct tcpm_port p={.state=SNK_READY,.pwr_role=TYPEC_SINK,.pd_count=2,
 .pd_capable=true,.pd_list=list,.selected_pd=&ids[0]};
 union power_supply_propval val;
 assert(!tcpm_psy_get_fixed_limit(&p,&val) && val.intval==5000000);
 assert(tcpm_psy_set_fixed_limit(&p,4999999)==-EINVAL);
 busy=1;assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EAGAIN);
 assert(p.selected_pd==&ids[0] && !p.psy_selected_profile && !p.nr_snk_pdo);
 busy=0;assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EAGAIN);
 assert(p.selected_pd==&ids[1] && p.psy_selected_profile && p.nr_snk_pdo==3);
 int before=starts;
 assert(tcpm_psy_get_fixed_limit(&p,&val)==-EAGAIN); /* Queued AMS is not READY. */
 p.update_sink_caps=false;
 assert(!tcpm_psy_set_fixed_limit(&p,9000000) && starts==before);
 p.state=SNK_NEGOTIATE_CAPABILITIES;
 assert(tcpm_psy_get_fixed_limit(&p,&val)==-EAGAIN);
 p.state=SNK_READY;p.pps_data.active=true;
 p.supply_voltage=8400;
 assert(tcpm_psy_set_fixed_limit(&p,5000000)==-EINVAL && starts==before);
 assert(!tcpm_psy_set_fixed_limit(&p,9500000) && p.psy_voltage_limit_mv==9500);
 p.pps_data.active=false;
 tcpm_psy_restore_profile(&p);
 assert(p.selected_pd==&ids[0] && p.nr_snk_pdo==2 && !p.psy_selected_profile);
 assert(!tcpm_psy_get_fixed_limit(&p,&val) && val.intval==5000000);
 p.pwr_role=TYPEC_SOURCE;assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EOPNOTSUPP);
 assert(p.selected_pd==&ids[0]);
 return 0;
}
