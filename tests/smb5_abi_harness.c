// SPDX-License-Identifier: AGPL-3.0-only
/* Actual standard SMB5 setter: error retention, units and exclusive ownership. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
#define system_wq NULL
#define mod_delayed_work(q,w,t) ((void)0)
#define dev_err(...) ((void)0)
#define UMI_CHARGE_USB_ICL_UA 3000000
#define CURRENT_SCALE_FACTOR 50000
#define USBIN_CURRENT_LIMIT_CFG 0x370
#define POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO 0
#define POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE 1
struct smb5_chip {
 bool stopping, pps_owned, test_armed, test_active, inhibit;
 unsigned int saved_icl_ua;
 int lock, status_change_work; unsigned int base; void *regmap;
};
struct power_supply { struct smb5_chip *chip; };
enum power_supply_property { POWER_SUPPLY_PROP_CURRENT_MAX,
 POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR };
union power_supply_propval { int intval; };
static struct smb5_chip *power_supply_get_drvdata(struct power_supply *p) {return p->chip;}
static int writes, notifications, stop_error;
static unsigned int raw_limit;
static int regmap_write(void *map, unsigned int reg, unsigned int value) {
 assert(reg==USBIN_CURRENT_LIMIT_CFG);raw_limit=value;writes++;return 0;
}
static void power_supply_changed(struct power_supply *p) { notifications++; }
static int smb5_inhibit_with_fallback(struct smb5_chip *c,const char *why) {return stop_error;}
/* MISEED_SMB5_ABI_UNDER_TEST */
int main(void) {
 struct smb5_chip c={0};struct power_supply psy={.chip=&c};
 union power_supply_propval v={.intval=1500000};
 assert(!smb5_set_property(&psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v));
 assert(raw_limit==30 && notifications==1 && c.saved_icl_ua==1500000);
 v.intval=-1;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)==-EINVAL);
 v.intval=3000001;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)==-EINVAL);
 assert(writes==1);
 v.intval=3000000;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_CURRENT_MAX,&v)==-EINVAL);
 c.pps_owned=true;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&v)==-EBUSY);
 v.intval=1;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&v)==-EBUSY);
 c.pps_owned=false;c.test_armed=true;c.test_active=true;stop_error=-EIO;
 assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&v)==-EIO);
 assert(c.inhibit && !c.test_armed && !c.test_active);
 stop_error=0;assert(!smb5_set_property(&psy,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&v));
 v.intval=2;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&v)==-EINVAL && c.inhibit);
 v.intval=0;assert(!smb5_set_property(&psy,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&v) && !c.inhibit);
 c.stopping=true;assert(smb5_set_property(&psy,POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,&v)==-EBUSY);
 assert(smb5_property_is_writable(&psy,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT));
 assert(!smb5_property_is_writable(&psy,POWER_SUPPLY_PROP_CURRENT_MAX));
 return 0;
}
