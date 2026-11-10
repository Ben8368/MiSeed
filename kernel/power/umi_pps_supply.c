// SPDX-License-Identifier: GPL-2.0-only
/*
 * Umi BQ25970 + mainline TCPM PPS integration.
 * Register encodings / 2:1 tuning adapted from LineageOS
 * android_kernel_xiaomi_sm8250 71b13e62f057a649b77fe4062feb73ee72ad609c.
 * BQ source: Copyright (C) 2017 Texas Instruments; GPL version 2.
 * Policy MODULE_AUTHOR: Fei Jiang <jiangfei1@xiaomi.com>.
 * Policy header credits: a0220433 (Mar 27, 2017). See NOTICE.md.
 * No vendor power_supply ABI, pmic-voter, charger authentication or QC writes.
 */
#include "umi_pps_internal.h"

static enum power_supply_property umi_pps_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

static int umi_pps_get_property(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val)
{
	struct umi_pps *c = power_supply_get_drvdata(psy);
	unsigned int status;
	int ret = 0, raw;

	mutex_lock(&c->lock);
	if (c->removing || !c->ready) {
		ret = c->removing ? -ENODEV : -EAGAIN;
		goto out;
	}
	switch (prop) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Texas Instruments";
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "BQ25970";
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &raw);
		if (!ret)
			val->intval = !!raw;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = regmap_read(c->map, 0x0a, &status);
		if (!ret)
			val->intval = status & BIT(2) ? POWER_SUPPLY_STATUS_CHARGING :
				POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		/* Fault flags are read and latched by the policy worker only. */
		val->intval = c->stop_pending ||
			(c->state == PPS_FAULT && c->last_error != -EOPNOTSUPP) ?
			POWER_SUPPLY_HEALTH_UNSPEC_FAILURE :
			(c->state == PPS_RUN ? POWER_SUPPLY_HEALTH_GOOD :
			 POWER_SUPPLY_HEALTH_UNKNOWN);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!c->configured) {
			ret = -ENODATA;
			break;
		}
		ret = umi_pps_adc(c, prop == POWER_SUPPLY_PROP_CURRENT_NOW ? 0 : 1, &raw);
		if (!ret)
			val->intval = raw * 1000; /* ADC mA/mV -> power_supply uA/uV */
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		ret = umi_pps_get(c->pd, prop, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		val->intval = c->manual_stop ? POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE :
			POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		break;
	default:
		ret = -EINVAL;
	}
out:
	mutex_unlock(&c->lock);
	return ret;
}

static int umi_pps_set_property(struct power_supply *psy,
		enum power_supply_property prop, const union power_supply_propval *val)
{
	struct umi_pps *c = power_supply_get_drvdata(psy);
	int ret = 0;

	if (prop != POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR ||
	    (val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
	     val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE))
		return -EINVAL;
	mutex_lock(&c->lock);
	if (c->removing || !c->ready) {
		ret = c->removing ? -ENODEV : -EAGAIN;
		goto out;
	}
	if (val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE) {
		/* Latch the intent even if stop fails. Retry retains SMB5 ownership. */
		c->manual_stop = true;
		ret = umi_pps_stop(c, PPS_OFF, 0);
	} else if (c->stop_pending || c->suspended || c->state == PPS_FAULT) {
		ret = -EBUSY;
	} else {
		c->manual_stop = false;
		if (c->state == PPS_OFF)
			c->state = PPS_WAIT_PLUG;
	}
	mod_delayed_work(system_wq, &c->work, 0);
out:
	mutex_unlock(&c->lock);
	power_supply_changed(psy);
	return ret;
}

static int umi_pps_property_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return prop == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR;
}

const struct power_supply_desc umi_pps_desc = {
	.name = "bq25970-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_C) | BIT(POWER_SUPPLY_USB_TYPE_PD) |
		BIT(POWER_SUPPLY_USB_TYPE_PD_PPS),
	.properties = umi_pps_properties,
	.num_properties = ARRAY_SIZE(umi_pps_properties),
	.get_property = umi_pps_get_property,
	.set_property = umi_pps_set_property,
	.property_is_writeable = umi_pps_property_is_writeable,
	.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
		BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
};
