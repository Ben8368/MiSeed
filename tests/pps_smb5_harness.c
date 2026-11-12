// SPDX-License-Identifier: AGPL-3.0-only
/* Exercises actual ownership functions without accessing registers or devices. */
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include "umi_pps_policy.h"
#define CHARGING_ENABLE_CMD 0x42
#define CHARGING_ENABLE_CMD_BIT 1
#define USBIN_CMD_IL 0x340
#define USBIN_SUSPEND_BIT 1
#define EXPORT_SYMBOL_GPL(x)
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
#define system_wq NULL
#define mod_delayed_work(q,w,t) ((void)0)
struct delayed_work { int unused; };
struct smb5_chip {
 bool stopping, pps_owned, test_armed, inhibit, enabled, full_bypass;
 int power_stage;
 int max_fv_uv, lock; unsigned int base; void *regmap;
 struct delayed_work status_change_work;
};
struct power_supply;
enum power_supply_property { PROP };
union power_supply_propval { int intval; };
struct desc { int (*set_property)(struct power_supply *,enum power_supply_property,const union power_supply_propval *); };
struct power_supply { struct desc *desc; struct smb5_chip *chip; };
static int smb5_set_property(struct power_supply *p,enum power_supply_property prop,const union power_supply_propval *v) { return 0; }
static struct smb5_chip *power_supply_get_drvdata(struct power_supply *p) { return p->chip; }
static int stop_error, write_error, read_error;
static unsigned int charge, input;
static int smb5_inhibit_with_fallback(struct smb5_chip *c,const char *where) {
 if (stop_error) return -EIO;
 charge=0;
 return 0;
}
static int regmap_update_bits(void *m,unsigned int r,unsigned int mask,unsigned int v) {
 if(write_error)return -EIO;
 assert(r==0x1340 && mask==1);input=v;return 0;
}
static int regmap_read(void *m,unsigned int r,unsigned int *v) {
 if(read_error)return -EIO;
 if(r==0x1042)*v=charge;
 else {assert(r==0x1340);*v=input;}
 return 0;
}
/* MISEED_SMB5_UNDER_TEST */
int main(void) {
 struct smb5_chip c={.base=0x1000,.max_fv_uv=4380000};
 struct desc d={.set_property=smb5_set_property};
 struct power_supply p={.desc=&d,.chip=&c};
 assert(umi_smb5_claim(NULL)==-ENODEV);
 c.inhibit=true;assert(umi_smb5_claim(&p)==-EBUSY && !c.pps_owned);
 c.inhibit=false;
 c.test_armed=true;assert(umi_smb5_claim(&p)==-EBUSY && !c.pps_owned);
 c.test_armed=false;c.stopping=true;assert(umi_smb5_claim(&p)==-EBUSY);
 c.stopping=false;charge=1;assert(!umi_smb5_claim(&p));
 assert(c.pps_owned && c.inhibit && !c.enabled && charge==0 && input==1);
 assert(umi_smb5_check(&p, 1)==4380000 && c.power_stage==1);
 assert(umi_smb5_claim(&p)==-EBUSY);
 charge=1;assert(umi_smb5_check(&p, 1)==-EPERM);charge=0;
 input=0;assert(umi_smb5_check(&p, 1)==-EPERM);input=1;
 read_error=1;assert(umi_smb5_check(&p, 1)==-EIO);read_error=0;
 assert(umi_smb5_check(&p, -1)==-EINVAL);
 stop_error=1;assert(umi_smb5_release(&p)==-EIO && c.pps_owned);
 stop_error=0;write_error=1;assert(umi_smb5_release(&p)==-EIO && c.pps_owned);
 write_error=0;assert(!umi_smb5_release(&p) && !c.pps_owned && !c.inhibit && input==0);
 assert(umi_smb5_check(&p, 1)==-EPERM);
 stop_error=1;assert(umi_smb5_claim(&p)==-EIO && c.pps_owned && c.inhibit);
 stop_error=0;assert(!umi_smb5_release(&p));
 write_error=1;assert(umi_smb5_claim(&p)==-EIO && c.pps_owned);
 write_error=0;assert(!umi_smb5_release(&p));
 assert(!umi_smb5_claim(&p));
 assert(umi_smb5_check(&p, 2)==4380000 && c.power_stage==2);
 assert(!umi_smb5_release(&p));
 assert(umi_smb5_claim(&p)==-EBUSY); /* No 70/71% repeated pump restart. */
 c.power_stage=1;
 assert(!umi_smb5_claim(&p));
 assert(umi_smb5_check(&p, 3)==4380000 && c.full_bypass);
 assert(!umi_smb5_release(&p) && !charge && input==0);
 assert(umi_smb5_claim(&p)==-EBUSY); /* Bypass cannot be overridden by PPS. */
 return 0;
}
