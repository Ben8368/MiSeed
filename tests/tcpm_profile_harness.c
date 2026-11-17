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
 bool pd_capable,psy_selected_profile,psy_pd_link_dirty,update_sink_caps;
 struct { bool active; } pps_data;
 struct pd_data **pd_list;struct usb_power_delivery *selected_pd;
 u32 snk_pdo[7];void *port_sink_caps,*typec_port; };
union power_supply_propval { int intval; };
static int busy, starts, fail_all_links;
static struct usb_power_delivery *linked, *fail_link;
static void tcpm_log(struct tcpm_port *p,const char *fmt, ...) { }
static int tcpm_ams_start(struct tcpm_port *p,int ams) { starts++;return busy?-EAGAIN:0; }
static int typec_port_set_usb_power_delivery(void *p,struct usb_power_delivery *pd) {
 if(linked==pd)return 0;
 if(pd && linked)return -EEXIST;
 if(pd && (fail_all_links || pd==fail_link))return -EIO;
 linked=pd;return 0;
}
/* DRIVER_UNDER_TEST */
int main(void) {
 struct usb_power_delivery ids[2]={{0},{1}};
 struct pd_data data[2]={
 {.sink_desc={{(5000/50<<10)|300,3U<<30}},.pd=&ids[0],.operating_snk_mw=7000},
 {.sink_desc={{(5000/50<<10)|300,(9000/50<<10)|300,3U<<30}},.pd=&ids[1],.operating_snk_mw=7000}};
 struct pd_data *list[]={&data[0],&data[1]};
 struct tcpm_port p={.state=SNK_READY,.pwr_role=TYPEC_SINK,.pd_count=2,
 .pd_capable=true,.pd_list=list,.selected_pd=&ids[0]};
 union power_supply_propval val;
 linked=&ids[0];
 assert(!tcpm_psy_check_pd(&p,&ids[1]));
 assert(!tcpm_psy_get_fixed_limit(&p,&val) && val.intval==5000000);
 assert(tcpm_psy_set_fixed_limit(&p,4999999)==-EINVAL);
 busy=1;assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EAGAIN);
 assert(p.selected_pd==&ids[0] && !p.psy_selected_profile && !p.nr_snk_pdo);
 assert(linked==&ids[0]);
 busy=0;fail_link=&ids[1];int old_starts=starts;
 assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EIO && linked==&ids[0]);
 assert(starts==old_starts && p.selected_pd==&ids[0] && !p.psy_selected_profile);
 fail_link=NULL;
 busy=0;assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EAGAIN);
 assert(p.selected_pd==&ids[1] && p.psy_selected_profile && p.nr_snk_pdo==3);
 assert((p.snk_pdo[1]&1023)==300); /* 9 V contract capability is 3 A. */
 assert(linked==&ids[1]);
 assert(tcpm_psy_check_pd(&p,&ids[0])==-EBUSY);
 assert(!tcpm_psy_check_pd(&p,&ids[1]));
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
 assert(tcpm_psy_check_pd(&p,&ids[0])==-EBUSY);
 p.pps_data.active=false;
 fail_link=&ids[0];tcpm_psy_restore_profile(&p);
 assert(p.selected_pd==&ids[0] && p.nr_snk_pdo==2 && p.psy_selected_profile);
 assert(tcpm_psy_get_fixed_limit(&p,&val)==-EIO);
 fail_link=NULL;
 tcpm_psy_restore_profile(&p);
 assert(p.selected_pd==&ids[0] && p.nr_snk_pdo==2 && !p.psy_selected_profile);
 assert(linked==&ids[0]);
 assert(!tcpm_psy_check_pd(&p,&ids[1]));
 assert(!tcpm_psy_get_fixed_limit(&p,&val) && val.intval==5000000);
 /* Both publication and restoration fail with a nonzero previous limit. */
 assert(!tcpm_psy_set_fixed_limit(&p,5000000));
 before=starts;fail_all_links=1;
 assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EIO);
 assert(!linked && p.psy_pd_link_dirty && p.psy_voltage_limit_mv==5000);
 assert(p.selected_pd==&ids[0] && starts==before);
 assert(tcpm_psy_get_fixed_limit(&p,&val)==-EIO);
 assert(tcpm_psy_set_fixed_limit(&p,5000000)==-EIO && !linked);
 assert(tcpm_psy_check_pd(&p,&ids[0])==-EIO);
 assert(tcpm_psy_check_pd(&p,&ids[1])==-EBUSY);
 fail_all_links=0;
 assert(!tcpm_psy_set_fixed_limit(&p,5000000));
 assert(linked==&ids[0] && !p.psy_pd_link_dirty && starts==before);
 assert(!tcpm_psy_get_fixed_limit(&p,&val) && val.intval==5000000);
 /* AMS rejection followed by two failed rollback publications. */
 busy=1;fail_link=&ids[0];
 assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EAGAIN);
 assert(!linked && p.psy_pd_link_dirty && p.psy_voltage_limit_mv==5000);
 assert(p.selected_pd==&ids[0] && p.upcoming_state==INVALID_STATE);
 assert(tcpm_psy_get_fixed_limit(&p,&val)==-EIO);
 fail_link=NULL;busy=0;
 assert(!tcpm_psy_check_pd(&p,&ids[0]));
 assert(linked==&ids[0] && !p.psy_pd_link_dirty);
 assert(!tcpm_psy_get_fixed_limit(&p,&val) && val.intval==5000000);
 /* Reset must also repair a failed initial selection with no owner yet. */
 tcpm_psy_restore_profile(&p);fail_all_links=1;
 assert(!p.psy_selected_profile && !p.psy_voltage_limit_mv);
 assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EIO && p.psy_pd_link_dirty);
 fail_all_links=0;tcpm_psy_restore_profile(&p);
 assert(linked==&ids[0] && !p.psy_pd_link_dirty && !p.psy_selected_profile);
 /* PPS cannot report a new ceiling while its publication is broken. */
 p.pps_data.active=true;p.supply_voltage=5000;fail_all_links=1;
 assert(tcpm_psy_link_pd(&p,&ids[1])==-EIO && p.psy_pd_link_dirty);
 assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EIO && !p.psy_voltage_limit_mv);
 fail_all_links=0;assert(!tcpm_psy_set_fixed_limit(&p,9000000));
 assert(linked==&ids[0] && !p.psy_pd_link_dirty && p.psy_voltage_limit_mv==9000);
 tcpm_psy_restore_profile(&p);p.pps_data.active=false;
 p.pwr_role=TYPEC_SOURCE;assert(tcpm_psy_set_fixed_limit(&p,9000000)==-EOPNOTSUPP);
 assert(p.selected_pd==&ids[0]);
 return 0;
}
