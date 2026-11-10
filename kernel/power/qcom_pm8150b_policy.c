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



/* Only this driver's power_supply may be passed to the PPS ownership API. */
static bool smb5_is_ours(struct power_supply *psy)
{
	return psy && psy->desc && psy->desc->set_property == smb5_set_property;
}

int umi_smb5_claim(struct power_supply *psy)
{
	struct smb5_chip *chip;
	int rc;

	if (!smb5_is_ours(psy))
		return -ENODEV;
	chip = power_supply_get_drvdata(psy);
	mutex_lock(&chip->lock);
	if (chip->stopping || chip->pps_owned || chip->test_armed || chip->inhibit) {
		rc = -EBUSY;
		goto out;
	}
	/* Keep ownership even after an I/O error; cleanup must verify the stop. */
	chip->pps_owned = true;
	chip->inhibit = true;
	chip->enabled = false;
	rc = smb5_inhibit_with_fallback(chip, "pps-claim");
	if (!rc)
		rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
out:
	mutex_unlock(&chip->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(umi_smb5_claim);

int umi_smb5_check(struct power_supply *psy)
{
	struct smb5_chip *chip;
	unsigned int charge, input;
	int rc;

	if (!smb5_is_ours(psy))
		return -ENODEV;
	chip = power_supply_get_drvdata(psy);
	mutex_lock(&chip->lock);
	rc = !chip->pps_owned || chip->stopping ? -EPERM : 0;
	if (!rc)
		rc = regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &charge);
	if (!rc)
		rc = regmap_read(chip->regmap, chip->base + USBIN_CMD_IL, &input);
	if (!rc && ((charge & CHARGING_ENABLE_CMD_BIT) || !(input & USBIN_SUSPEND_BIT)))
		rc = -EPERM;
	if (!rc)
		rc = chip->max_fv_uv;
	mutex_unlock(&chip->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(umi_smb5_check);

int umi_smb5_release(struct power_supply *psy)
{
	struct smb5_chip *chip;
	int rc;

	if (!smb5_is_ours(psy))
		return -ENODEV;
	chip = power_supply_get_drvdata(psy);
	mutex_lock(&chip->lock);
	rc = smb5_inhibit_with_fallback(chip, "pps-release");
	if (!rc)
		rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					USBIN_SUSPEND_BIT, 0);
	if (!rc) {
		chip->pps_owned = false;
		chip->inhibit = false;
	}
	mutex_unlock(&chip->lock);
	if (!rc)
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
	return rc;
}
EXPORT_SYMBOL_GPL(umi_smb5_release);

int smb5_get_prop_health(struct smb5_chip *chip, int *val);

static int smb5_battery_value(struct smb5_chip *chip,
                             enum power_supply_property property, int *value)
{
 union power_supply_propval val;
 int rc = power_supply_get_property(chip->bms, property, &val);
 if (!rc)
  *value = val.intval;
 return rc;
}

static int smb5_limits_verified(struct smb5_chip *chip)
{
 static const unsigned int regs[] = { FAST_CHARGE_CURRENT_CFG, USBIN_CURRENT_LIMIT_CFG,
                                     FLOAT_VOLTAGE_CFG, WD_CFG, SNARL_BARK_BITE_WD_CFG, 0x670 };
 unsigned int v[ARRAY_SIZE(regs)];
 int rc, i;
 for (i = 0; i < ARRAY_SIZE(regs); i++) {
  rc = regmap_read(chip->regmap, chip->base + regs[i], &v[i]);
  if (rc) return rc;
 }
	if (v[0] != UMI_CHARGE_FCC_UA / CURRENT_SCALE_FACTOR ||
	    v[1] > UMI_CHARGE_USB_ICL_UA / CURRENT_SCALE_FACTOR ||
     3600000 + (int)v[2] * 10000 != chip->max_fv_uv ||
     !(v[3] & WDOG_TIMER_EN_BIT) ||
     !(v[4] & BITE_WDOG_DISABLE_CHARGING_CFG_BIT) || !(v[5] & BIT(0)))
  return -EPERM;
 return 0;
}

#include "umi_smb5_diagnostics.h"

void smb5_status_change_work(struct work_struct *work)
{
 struct smb5_chip *chip = container_of(work, struct smb5_chip, status_change_work.work);
 int online = 0, present = 0, health = POWER_SUPPLY_HEALTH_UNKNOWN;
 int temp = -10000, voltage = -1, soc = -1, bms_current = 0;
 int rc, usb_rc, allow = 0, stop_rc;
 mutex_lock(&chip->lock);
 if (chip->stopping) {
  mutex_unlock(&chip->lock);
  return;
 }
 smb5_log_registers(chip);
 usb_rc = smb5_get_prop_usb_online(chip, &online);
 rc = usb_rc;
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_PRESENT, &present);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_TEMP, &temp);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_CAPACITY, &soc);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_CURRENT_NOW, &bms_current);
 if (!rc) rc = smb5_get_prop_health(chip, &health);
 if (!rc) rc = smb5_limits_verified(chip);
	if (!rc && bms_current > UMI_CHARGE_FCC_UA) rc = -EOVERFLOW;
 if (!usb_rc && !online) {
  if (chip->test_active) {
   chip->test_active = false;
   chip->test_armed = false;
   chip->inhibit = true;
  } else if (chip->test_armed) {
   chip->test_saw_disconnect = true;
  }
 }
 if (!rc && online && chip->test_armed && chip->test_saw_disconnect && !chip->test_active) {
  chip->test_active = true;
  chip->test_deadline = jiffies + chip->test_seconds * HZ;
  dev_info(chip->dev, "miseed-charge test window started: %u seconds maximum\n", chip->test_seconds);
 }
 if (chip->test_active && time_after_eq(jiffies, chip->test_deadline)) {
  chip->test_active = false;
  chip->test_armed = false;
  chip->inhibit = true;
 }
 if (!rc && !chip->inhibit && (!chip->test_armed || chip->test_active))
  allow = umi_charge_policy(online, present, health == POWER_SUPPLY_HEALTH_GOOD,
                           temp, voltage, soc, chip->max_fv_uv,
                           chip->enabled, chip->test_active);
 if (allow) {
  /* Limits are set with charging disabled, before probe registers IRQ/work. */
  rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL, USBIN_SUSPEND_BIT, 0);
  if (!rc) rc = regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET, BARK_BITE_WDOG_PET_BIT);
  if (!rc) rc = smb5_set_prop_charging_enabled(chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO);
 }
 if (!allow || rc) {
  stop_rc = smb5_inhibit_with_fallback(chip, "worker");
  if (!rc) rc = stop_rc;
 }
 if (rc && chip->test_armed) {
  chip->test_active = false;
  chip->test_armed = false;
  chip->inhibit = true;
 }
 chip->enabled = allow && !rc;
 dev_info(chip->dev, "miseed-charge enabled=%u input=%d present=%d health=%d temp_decic=%d vbat_uv=%d current_ua=%d soc=%d test_armed=%u test_active=%u rc=%d fcc_ua=%u icl_ua=%u fv_uv=%d\n",
          chip->enabled, online, present, health, temp, voltage, bms_current, soc,
          chip->test_armed, chip->test_active, rc,
			UMI_CHARGE_FCC_UA, UMI_CHARGE_USB_ICL_UA, chip->max_fv_uv);
 mutex_unlock(&chip->lock);
 power_supply_changed(chip->chg_psy);
 schedule_delayed_work(&chip->status_change_work, msecs_to_jiffies(1000));
}

#ifdef CONFIG_MISEED_CHARGE_DIAGNOSTICS
static ssize_t charge_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
 struct smb5_chip *chip = power_supply_get_drvdata(dev_get_drvdata(dev));
 ssize_t n;
 mutex_lock(&chip->lock);
 n = sysfs_emit(buf, "armed=%u active=%u seconds_max=%u\n",
                chip->test_armed, chip->test_active, chip->test_seconds);
 mutex_unlock(&chip->lock);
 return n;
}

static ssize_t charge_test_store(struct device *dev, struct device_attribute *attr,
                                 const char *buf, size_t count)
{
 struct smb5_chip *chip = power_supply_get_drvdata(dev_get_drvdata(dev));
 int rc;
 if (!sysfs_streq(buf, "arm") && !sysfs_streq(buf, "arm-10min") &&
     !sysfs_streq(buf, "stop")) return -EINVAL;
 mutex_lock(&chip->lock);
 if (chip->pps_owned) {
  mutex_unlock(&chip->lock);
  return -EBUSY;
 }
 chip->test_armed = !sysfs_streq(buf, "stop");
 chip->test_seconds = sysfs_streq(buf, "arm-10min") ?
                      UMI_CHARGE_EXTENDED_TEST_SECONDS : UMI_CHARGE_TEST_SECONDS;
 chip->inhibit = !chip->test_armed;
 chip->test_active = false;
 chip->test_saw_disconnect = false;
 chip->enabled = false;
 rc = smb5_inhibit_with_fallback(chip, "test-arm");
 if (rc) {
  chip->test_armed = false;
  chip->inhibit = true;
 }
 mutex_unlock(&chip->lock);
 if (rc) return rc;
 mod_delayed_work(system_wq, &chip->status_change_work, 0);
 return count;
}
static DEVICE_ATTR_RW(charge_test);
static struct attribute *smb5_attrs[] = { &dev_attr_charge_test.attr, NULL };
const struct attribute_group smb5_group = { .attrs = smb5_attrs };
#endif
