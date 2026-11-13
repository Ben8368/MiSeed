// SPDX-License-Identifier: GPL-2.0-only
/* Umi wired SMB5 thermal stop, using the existing battery-temperature gate.
 * Register/charger attribution is retained in qcom_pm8150b_core.c/NOTICE.md.
 * No PPS virtual-temperature or wireless RX/TX model is shared here.
 */
#include "qcom_pm8150b_internal.h"

static int smb5_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct smb5_chip *chip = thermal_zone_device_priv(tz);
	union power_supply_propval value;
	int ret;

	mutex_lock(&chip->lock);
	if (chip->stopping) {
		ret = -ENODEV;
		goto out;
	}
	ret = power_supply_get_property(chip->bms, POWER_SUPPLY_PROP_TEMP, &value);
	if (!ret) {
		/* power_supply is deci-C; thermal is milli-C. Reject ADC rails. */
		if (value.intval <= -400 || value.intval >= 1250)
			ret = -ERANGE;
		else
			*temp = value.intval * 100;
	}
out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int smb5_thermal_get_max(struct thermal_cooling_device *dev,
			      unsigned long *state)
{
	*state = 1;
	return 0;
}

static int smb5_thermal_get_state(struct thermal_cooling_device *dev,
				unsigned long *state)
{
	struct smb5_chip *chip = dev->devdata;

	mutex_lock(&chip->lock);
	*state = chip->cooling_state;
	mutex_unlock(&chip->lock);
	return 0;
}

static int smb5_thermal_set_state(struct thermal_cooling_device *dev,
				unsigned long state)
{
	struct smb5_chip *chip = dev->devdata;
	int ret = 0;

	if (state > 1)
		return -EINVAL;
	mutex_lock(&chip->lock);
	if (chip->stopping) {
		ret = -ENODEV;
		goto out;
	}
	/* Keep the request latched on stop failure; the worker retries it.
	 * Clearing cooling never clears a user's inhibit or enables hardware.
	 * While PPS owns the path, SMB5 is already off with its input suspended;
	 * only the PPS owner may release that interlock.
	 */
	chip->cooling_state = state;
	if (state && !chip->pps_owned) {
		ret = smb5_inhibit_with_fallback(chip, "thermal");
		chip->enabled = false;
	}
	mod_delayed_work(system_wq, &chip->status_change_work, 0);
out:
	mutex_unlock(&chip->lock);
	return ret;
}

static const struct thermal_zone_device_ops smb5_thermal_ops = {
	.get_temp = smb5_thermal_get_temp,
};

static const struct thermal_cooling_device_ops smb5_cooling_ops = {
	.get_max_state = smb5_thermal_get_max,
	.get_cur_state = smb5_thermal_get_state,
	.set_cur_state = smb5_thermal_set_state,
};

int smb5_register_thermal(struct smb5_chip *chip)
{
	struct thermal_cooling_device *cooling;
	struct thermal_zone_device *zone;

	cooling = devm_thermal_of_cooling_device_register(chip->dev,
		chip->dev->of_node, "umi-wired-pd", chip, &smb5_cooling_ops);
	if (IS_ERR(cooling))
		return PTR_ERR(cooling);
	zone = devm_thermal_of_zone_register(chip->dev, 0, chip, &smb5_thermal_ops);
	return PTR_ERR_OR_ZERO(zone);
}
