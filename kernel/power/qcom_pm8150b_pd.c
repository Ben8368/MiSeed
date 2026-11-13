// SPDX-License-Identifier: GPL-2.0-only
/* Wired PM8150B fixed PD coordination. Register/FSW facts from pinned LOS
 * smb5-reg.h / smb5-lib.c / qpnp-smb5.c, 71b13e62. See NOTICE.md.
 * TCPM owns negotiation; SMB5 owns the buck and battery/input inhibition.
 */
#include "qcom_pm8150b_internal.h"

static int smb5_pd_get(struct smb5_chip *chip, enum power_supply_property prop, int *v)
{
	union power_supply_propval val;
	int ret = power_supply_get_property(chip->pd, prop, &val);

	if (!ret)
		*v = val.intval;
	return ret;
}

/* Temperature-trend feedback adds no new temperature trip or sensor model.
 * Each warming observation removes one existing 100 mA policy step. After
 * cooling, recover at most one step per 1 s worker; hard gates remain separate.
 */
void smb5_thermal_limit(struct smb5_chip *chip, int temp)
{
	if (!chip->thermal_sampled) {
		chip->thermal_drop_ua = 0;
		chip->thermal_sampled = true;
		chip->thermal_recovery_jiffies = jiffies;
	} else if (temp > chip->thermal_last_decic) {
		chip->thermal_drop_ua = min(chip->thermal_drop_ua + 100000,
					 (unsigned int)UMI_CHARGE_USB_ICL_UA);
		chip->thermal_recover = false;
		chip->thermal_recovery_jiffies = jiffies;
	} else if (temp < chip->thermal_last_decic) {
		chip->thermal_recover = true;
	}
	if (chip->thermal_recover &&
	    time_after_eq(jiffies, chip->thermal_recovery_jiffies + HZ)) {
		chip->thermal_drop_ua = chip->thermal_drop_ua > 100000 ?
			chip->thermal_drop_ua - 100000 : 0;
		chip->thermal_recovery_jiffies = jiffies;
	}
	chip->thermal_last_decic = temp;
}

int smb5_fixed_update(struct smb5_chip *chip)
{
	union power_supply_propval limit;
	int type, online, selected, voltage, input_ua, ret, input;
	bool fast;

	ret = smb5_pd_get(chip, POWER_SUPPLY_PROP_USB_TYPE, &type);
	if (ret)
		return ret;
	if (type != POWER_SUPPLY_USB_TYPE_PD && type != POWER_SUPPLY_USB_TYPE_PD_PPS) {
		ret = regmap_write(chip->regmap, chip->base + 0x344, BIT(0));
		if (!ret)
			ret = regmap_write(chip->regmap, chip->base + 0x150, 15);
		if (ret)
			return ret;
		chip->pd_voltage_uv = 5000000;
		chip->pd_current_ua = UMI_CHARGE_USB_ICL_UA;
		if (chip->pd_transition) {
			ret = -EAGAIN;
			goto failed;
		}
		return 0;
	}
	/* PPS-capable sources keep the safe profile for pump entry/exit at 5 V.
	 * Only the no-APDO fixed-PD path selects the 9 V/2 A profile.
	 */
	fast = type == POWER_SUPPLY_USB_TYPE_PD && chip->power_stage == 1 &&
		!chip->full_bypass && !chip->inhibit && !chip->cooling_state && !chip->pps_fault &&
		chip->thermal_last_decic > UMI_CHARGE_COOL_DECIC &&
		chip->thermal_last_decic <= UMI_CHARGE_WARM_DECIC;
	limit.intval = fast ? 9000000 : 5000000;
	if (!chip->pd_transition && chip->pd_limit_uv == limit.intval) {
		ret = smb5_pd_get(chip, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &selected);
		if (!ret && selected == limit.intval)
			goto verify;
		if (ret && ret != -EAGAIN)
			return ret;
	}
	if (!chip->pd_transition) {
		chip->pd_transition = true;
		chip->pd_deadline = jiffies + 10 * HZ;
	}
	ret = smb5_inhibit_with_fallback(chip, "pd-transition");
	chip->enabled = false;
	if (!ret)
		ret = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					 USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
	/* At 9 V: 1050 kHz => raw 8; retain that FSW through a downshift. */
	if (!ret)
		ret = regmap_write(chip->regmap, chip->base + 0x150, 8);
	if (!ret)
		ret = regmap_write(chip->regmap, chip->base + 0x344, BIT(3));
	if (!ret)
		ret = power_supply_set_property(chip->pd, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &limit);
	if (!ret)
		ret = smb5_pd_get(chip, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &selected);
	if (!ret && selected != limit.intval)
		ret = -EAGAIN;
	if (ret)
		goto failed;
	chip->pd_limit_uv = limit.intval;
verify:
	ret = smb5_pd_get(chip, POWER_SUPPLY_PROP_ONLINE, &online);
	if (!ret)
		ret = smb5_pd_get(chip, POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
	if (!ret)
		ret = smb5_pd_get(chip, POWER_SUPPLY_PROP_CURRENT_NOW, &input_ua);
	if (!ret && (online != 1 || (voltage != 5000000 && voltage != 9000000) ||
		voltage > limit.intval || input_ua <= 0 ||
		input_ua > (voltage == 9000000 ? 2000000 : UMI_CHARGE_USB_ICL_UA)))
		ret = -EPROTO;
	if (!ret)
		ret = smb5_get_iio_chan(chip, chip->usb_in_v_chan, &input);
	/* Reuse the existing no-load settle tolerance, not a new cable model. */
	if (!ret && chip->pd_transition && (input <= 0 || abs(input - voltage) > 200000))
		ret = -ERANGE;
	if (ret)
		goto failed;
	if (chip->pd_transition) {
		ret = regmap_write(chip->regmap, chip->base + 0x344,
				   voltage == 9000000 ? BIT(1) : BIT(0));
		if (!ret)
			ret = regmap_write(chip->regmap, chip->base + 0x150,
					   voltage == 9000000 ? 8 : 15);
		if (ret)
			goto failed;
	}
	chip->pd_voltage_uv = voltage;
	chip->pd_current_ua = input_ua;
	chip->pd_transition = false;
	return 0;
failed:
	if (ret == -EAGAIN && time_before(jiffies, chip->pd_deadline))
		return ret;
	chip->pps_fault = true; /* No automatic charging after an unverified shift. */
	return ret == -EAGAIN ? -ETIMEDOUT : ret;
}
