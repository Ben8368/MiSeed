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

/* TCPM CURRENT_NOW is the negotiated budget, not an ADC measurement.
 * Monitor the buck separately while the fixed contract is carrying load.
 * Do not compare a live ADC sample against a just-lowered thermal ICL:
 * those observations are not simultaneous and no settling model is qualified.
 */
static void smb5_fixed_fault(struct smb5_chip *chip, int ret)
{
	int stop_ret, suspend_ret;

	chip->pps_fault = true;
	chip->enabled = false;
	/* Attempt both stops even if battery inhibition fails. Never feed WD. */
	stop_ret = smb5_inhibit_with_fallback(chip, "fixed-pd-fault");
	suspend_ret = regmap_update_bits(chip->regmap,
		chip->base + USBIN_CMD_IL, USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
	dev_err(chip->dev, "miseed-fixed-pd fault rc=%d stop_rc=%d suspend_rc=%d\n",
		ret, stop_ret, suspend_ret);
}

/* All callers hold chip->lock. Preserve every sample, including the one
 * returned through power_supply; protection must not depend on which reader
 * happened to observe an excursion between worker polls.
 */
static int smb5_fixed_check_current(struct smb5_chip *chip, int contract_ua,
				  int measured_ua, const char *source)
{
	unsigned int icl_ua;
	int ret;

	ret = smb5_get_current_limit(chip, &icl_ua);
	if (ret)
		return ret;
	if (icl_ua > (unsigned int)contract_ua || measured_ua > contract_ua) {
		dev_err(chip->dev,
			"miseed-fixed-pd input-overlimit measured_ua=%d icl_ua=%u contract_ua=%d source=%s\n",
			measured_ua, icl_ua, contract_ua, source);
		return -EOVERFLOW;
	}
	dev_info(chip->dev,
		 "miseed-fixed-pd sample measured_ua=%d icl_ua=%u contract_ua=%d source=%s\n",
		 measured_ua, icl_ua, contract_ua, source);
	return 0;
}

int smb5_get_input_current_now(struct smb5_chip *chip, int *current_ua)
{
	int contract_ua, ret, check_ret = 0;

	mutex_lock(&chip->lock);
	ret = smb5_get_input_current(chip, current_ua);
	if (!chip->pps_owned && !chip->pd_transition && !chip->pps_fault &&
	    !chip->stopping && chip->pd_limit_uv) {
		/* The source may renegotiate between worker polls. */
		check_ret = ret ? ret : smb5_pd_get(chip,
			POWER_SUPPLY_PROP_CURRENT_NOW, &contract_ua);
		if (!check_ret && (contract_ua <= 0 || contract_ua > UMI_CHARGE_USB_ICL_UA))
			check_ret = -EPROTO;
		if (!check_ret)
			check_ret = smb5_fixed_check_current(chip, contract_ua,
				*current_ua, "power_supply");
		if (check_ret)
			smb5_fixed_fault(chip, check_ret);
	}
	/* Queue before dropping the lock: teardown sets stopping under this
	 * same lock, then cancels work. A late queue after unlock could escape it.
	 */
	if (check_ret)
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
	mutex_unlock(&chip->lock);
	/* A successfully read overlimit is still the original ADC value. */
	return ret;
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
	 * Only the no-APDO fixed-PD path selects the 9 V/3 A contract profile;
	 * SMB5's 18 W stage and 2 A ICL still limit actual wired input power.
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
		input_ua > UMI_CHARGE_USB_ICL_UA))
		ret = -EPROTO;
	/* Loaded PM8150B USBIN has the same erratum as the public getter.
	 * The common selector uses MID while loaded, USBIN only when suspended.
	 */
	if (!ret)
		ret = smb5_get_input_voltage(chip, &input);
	/* Reuse the existing no-load settle tolerance, not a new cable model. */
	if (!ret && chip->pd_transition && (input <= 0 || abs(input - voltage) > 200000))
		ret = -ERANGE;
	/* A transition is deliberately unloaded; apply its new ICL before the
	 * worker restores input. Only monitor the previous limit under load.
	 */
	if (!ret && !chip->pd_transition) {
		int measured_ua;

		ret = smb5_get_input_current(chip, &measured_ua);
		if (!ret)
			ret = smb5_fixed_check_current(chip, input_ua, measured_ua, "worker");
	}
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
	smb5_fixed_fault(chip, ret);
	return ret == -EAGAIN ? -ETIMEDOUT : ret;
}
