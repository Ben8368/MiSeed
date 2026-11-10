// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Teguh Sobirin <teguh@sobir.in>.
 *
 * This driver is for the switch-mode battery charger power delivery
 * and boost hardware found in pm8150b and related PMICs.
 * This work based on pmi8998 charger driver by
 * Caleb Connolly <caleb.connolly@linaro.org>
 * Should be merged with the existing charger driver in the future.
 */

/* MiSeed: Umi runtime integration. Standard CHARGE_BEHAVIOUR API,
 * fail-closed polling, LOS-derived limits and automatic charge policy.
 */

#include "qcom_pm8150b_internal.h"

static enum power_supply_property smb5_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
};

static int smb5_get_charge_control_threshold(struct smb5_chip *chip, bool end)
{
	int threshold;

	mutex_lock(&chip->lock);
	threshold = end ? UMI_CHARGE_END_SOC : UMI_CHARGE_START_SOC;
	mutex_unlock(&chip->lock);

	return threshold;
}

static int smb5_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);
	int rc, behaviour;
	unsigned int current_max;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = regmap_read(chip->regmap, chip->base + AICL_STATUS, &current_max);
		if (!rc)
			val->intval = current_max * CURRENT_SCALE_FACTOR;
		return rc;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return smb5_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return smb5_get_input_current(chip, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb5_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb5_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb5_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb5_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb5_apsd_get_charger_type(chip, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		val->intval = smb5_get_charge_control_threshold(chip, true);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		val->intval = smb5_get_charge_control_threshold(chip, false);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		mutex_lock(&chip->lock);
		rc = smb5_get_prop_charging_enabled(chip, &behaviour);
		if (!rc && chip->inhibit && behaviour != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
			rc = -EIO;
		if (!rc)
			val->intval = chip->inhibit ? POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE :
				POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		mutex_unlock(&chip->lock);
		return rc;
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

int smb5_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		mutex_lock(&chip->lock);
		rc = chip->pps_owned || chip->stopping ? -EBUSY : smb5_set_current_limit(chip, val->intval);
		mutex_unlock(&chip->lock);
		power_supply_changed(psy);
		return rc;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
                    val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
                        return -EINVAL;
                mutex_lock(&chip->lock);
                if (chip->pps_owned || chip->stopping) {
                        mutex_unlock(&chip->lock);
                        return -EBUSY;
                }
                chip->test_armed = false;
                chip->test_active = false;
                chip->inhibit = val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
                if (chip->inhibit)
                        rc = smb5_inhibit_with_fallback(chip, "user-inhibit");
                mutex_unlock(&chip->lock);
                mod_delayed_work(system_wq, &chip->status_change_work, 0);
                power_supply_changed(psy);
                return rc;
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return 1;
	default:
		return 0;
	}
}

const struct power_supply_desc smb5_psy_desc = {
	.name = "pm8150b_charger",
	.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
			     BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb5_properties,
	.num_properties = ARRAY_SIZE(smb5_properties),
	.get_property = smb5_get_property,
	.set_property = smb5_set_property,
	.property_is_writeable = smb5_property_is_writable,
};
