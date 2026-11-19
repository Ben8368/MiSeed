// SPDX-License-Identifier: GPL-2.0-only
/* Chip-owned standard telemetry. Reading sysfs never consumes fault latches. */
#include <linux/device.h>
#include <linux/property.h>
#include "bq25970_internal.h"

static enum power_supply_property bq25970_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER, POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW,
};
static int bq25970_get_property(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val)
{
	struct bq25970_device *bq = power_supply_get_drvdata(psy);
	bool switching;
	int ret = 0;

	if (prop == POWER_SUPPLY_PROP_STATUS) {
		ret = bq25970_is_switching(bq, &switching);
		if (!ret)
			val->intval = switching ? POWER_SUPPLY_STATUS_CHARGING :
				POWER_SUPPLY_STATUS_NOT_CHARGING;
		return ret;
	}
	mutex_lock(&bq->lock);
	switch (prop) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Texas Instruments";
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "BQ25970";
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = bq->cached.hardware_fault ? POWER_SUPPLY_HEALTH_UNSPEC_FAILURE :
			(!bq->sample_valid || bq->sample_error ? POWER_SUPPLY_HEALTH_UNKNOWN :
			 POWER_SUPPLY_HEALTH_GOOD);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!bq->configured || !bq->sample_valid)
			ret = bq->sample_error ? bq->sample_error : -ENODATA;
		else
			val->intval = prop == POWER_SUPPLY_PROP_VOLTAGE_NOW ?
				bq->cached.vbus_uv : bq->cached.ibus_ua;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&bq->lock);
	return ret;
}
int bq25970_register_supply(struct bq25970_device *bq)
{
	struct power_supply_config config = {
		.drv_data = bq, .fwnode = dev_fwnode(bq->dev),
	};
	struct power_supply *psy;

	bq->desc.name = devm_kasprintf(bq->dev, GFP_KERNEL, "bq25970-%s", dev_name(bq->dev));
	if (!bq->desc.name)
		return -ENOMEM;
	/* A DC charger input, independent of its upstream USB/PD transport. */
	bq->desc.type = POWER_SUPPLY_TYPE_MAINS;
	bq->desc.properties = bq25970_properties;
	bq->desc.num_properties = ARRAY_SIZE(bq25970_properties);
	bq->desc.get_property = bq25970_get_property;
	psy = devm_power_supply_register(bq->dev, &bq->desc, &config);
	if (IS_ERR(psy))
		return PTR_ERR(psy);
	bq->psy = psy;
	return 0;
}
