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
#include "umi_pps_policy.h"

/* Fixed PD: 10 W / 18 W at 9 V (15 W at 5 V) / 10 W. */
int smb5_power_limit(struct smb5_chip *chip, int soc)
{
	unsigned int icl_ua, target;
	int input_uv;
	int target_mw;
	int rc = smb5_get_current_limit(chip, &icl_ua);

	if (rc)
		return rc;
	if (!chip->policy_icl_valid || icl_ua != chip->policy_icl_ua)
		chip->saved_icl_ua = icl_ua;
	chip->power_stage = umi_pps_max(chip->power_stage, umi_pps_soc_stage(soc));
	{
		target_mw = chip->power_stage == 0 ? UMI_WIRED_LOW_MW :
			chip->power_stage >= 2 ? UMI_WIRED_FINISH_MW :
			chip->pd_voltage_uv == 9000000 ? UMI_WIRED_PD_9V_MW : UMI_WIRED_PD_FAST_MW;
		rc = smb5_get_input_voltage(chip, &input_uv);
		if (rc)
			return rc;
		if (input_uv <= 0)
			return -ERANGE;
		target = (unsigned int)((long long)target_mw * 1000000000LL /
				input_uv);
		target = umi_pps_min(target, chip->power_stage != 1 ||
			chip->pd_voltage_uv == 9000000 ? 2000000 : UMI_CHARGE_USB_ICL_UA);
	}
	if (chip->pd_current_ua > 0)
		target = umi_pps_min(target, chip->pd_current_ua);
	if (!chip->full_bypass)
		target = target > chip->thermal_drop_ua ? target - chip->thermal_drop_ua : 0;
	target = umi_pps_min(target, chip->saved_icl_ua);
	/* Match the hardware's 50 mA encoding before comparing our next readback. */
	target = target / CURRENT_SCALE_FACTOR * CURRENT_SCALE_FACTOR;
	if (icl_ua < target)
		target = umi_pps_min(target, icl_ua + 100000);
	/* Tightening a stage/thermal/contract ceiling takes effect immediately. */
	if (icl_ua != target)
		rc = smb5_set_current_limit(chip, target);
	if (!rc) {
		chip->policy_icl_ua = target;
		chip->policy_icl_valid = true;
	}
	return rc;
}

/* The wired input is shared with TCPM. APSD only describes BC1.2 and
 * must not hide an attached PD/PPS source from standard power_supply users.
 * Keep supplier reads outside chip->lock (TCPM has its own port lock).
 */
static int smb5_get_usb_type(struct smb5_chip *chip, int *type)
{
	union power_supply_propval val;
	int online, ret;

	ret = smb5_get_prop_usb_online(chip, &online);
	if (ret)
		return ret;
	if (!online) {
		*type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return 0;
	}
	if (chip->pd) {
		ret = power_supply_get_property(chip->pd, POWER_SUPPLY_PROP_USB_TYPE, &val);
		if (ret)
			return ret;
		if (val.intval == POWER_SUPPLY_USB_TYPE_PD ||
		    val.intval == POWER_SUPPLY_USB_TYPE_PD_PPS ||
		    val.intval == POWER_SUPPLY_USB_TYPE_PD_DRP) {
			*type = val.intval;
			return 0;
		}
	}
	return smb5_apsd_get_charger_type(chip, type);
}

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
		return smb5_get_input_current_now(chip, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		mutex_lock(&chip->lock);
		rc = smb5_get_input_voltage(chip, &val->intval);
		mutex_unlock(&chip->lock);
		return rc;
	case POWER_SUPPLY_PROP_ONLINE:
		return smb5_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb5_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb5_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb5_get_usb_type(chip, &val->intval);
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
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT: {
		unsigned int applied;

		if (val->intval < 0 || val->intval > UMI_CHARGE_USB_ICL_UA)
			return -EINVAL;
		mutex_lock(&chip->lock);
		if (chip->pps_owned || chip->stopping) {
			rc = -EBUSY;
			goto limit_out;
		}
		rc = smb5_get_current_limit(chip, &applied);
		if (rc)
			goto limit_out;
		/* A user request is a ceiling, never permission to override the
		 * live PD/thermal/power policy. Tighten immediately; increases
		 * require a fresh worker safety sample and its normal ramp.
		 */
		if ((unsigned int)val->intval < applied) {
			applied = val->intval / CURRENT_SCALE_FACTOR * CURRENT_SCALE_FACTOR;
			rc = smb5_set_current_limit(chip, applied);
		}
		if (!rc) {
			chip->saved_icl_ua = val->intval / CURRENT_SCALE_FACTOR * CURRENT_SCALE_FACTOR;
			chip->policy_icl_ua = applied;
			chip->policy_icl_valid = true;
			mod_delayed_work(system_wq, &chip->status_change_work, 0);
		}
limit_out:
		mutex_unlock(&chip->lock);
		if (!rc)
			power_supply_changed(psy);
		return rc;
	}
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
                    val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
                        return -EINVAL;
                mutex_lock(&chip->lock);
                if (chip->pps_owned || chip->stopping) {
                        mutex_unlock(&chip->lock);
                        return -EBUSY;
                }
                chip->inhibit = val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
                if (chip->inhibit)
                        rc = smb5_inhibit_with_fallback(chip, "user-inhibit");
                mod_delayed_work(system_wq, &chip->status_change_work, 0);
                mutex_unlock(&chip->lock);
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
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_PD) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_PPS) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_DRP) |
		     BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb5_properties,
	.num_properties = ARRAY_SIZE(smb5_properties),
	.get_property = smb5_get_property,
	.set_property = smb5_set_property,
	.property_is_writeable = smb5_property_is_writable,
};
