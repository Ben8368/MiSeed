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

int smb5_get_prop_usb_online(struct smb5_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + USBIN_INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read USBIN_RT_STS rc=%d\n", rc);
		return rc;
	}

	*val = (stat & USBIN_PLUGIN_RT_STS_BIT) ? 1 : 0;
	return 0;
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
int smb5_apsd_get_charger_type(struct smb5_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd status, rc = %d", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_err(chip->dev, "Apsd not ready");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd result, rc = %d", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	} else if (stat & DCP_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & OCP_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & FLOAT_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & QC_2P0_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & QC_3P0_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else {
		*val = POWER_SUPPLY_USB_TYPE_SDP;
	}

	return 0;
}

int smb5_get_prop_charging_enabled(struct smb5_chip *chip, int *val)
{
	unsigned int charging_enabled;
	int rc = regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &charging_enabled);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging enabled status, rc = %d", rc);
		return rc;
	}
	*val = charging_enabled & CHARGING_ENABLE_CMD_BIT ?
		POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO :
		POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
	return 0;
}

int smb5_get_prop_status(struct smb5_chip *chip, int *val)
{
	unsigned char stat[2];
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	rc = regmap_bulk_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, &stat, 2);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n",
			rc);
		return rc;
	}

	if (stat[1] & VBATT_GTET_INHIBIT_BIT) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	stat[0] = stat[0] & BATTERY_CHARGER_STATUS_MASK;

	switch (stat[0]) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return rc;
	case INHIBIT_CHARGE:
	case PAUSE_CHARGE:
	case DISABLE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return rc;
	case TERMINATE_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return rc;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return rc;
	}
}

int smb5_get_current_limit(struct smb5_chip *chip,
					 unsigned int *val)
{
	int rc = regmap_read(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG, val);

	if (rc >= 0)
		*val *= CURRENT_SCALE_FACTOR;
	return rc;
}

int smb5_set_current_limit(struct smb5_chip *chip, unsigned int val)
{
	unsigned char val_raw;

	if (val > UMI_CHARGE_USB_ICL_UA) {
		dev_err(chip->dev,
			"LOS Umi USB input current limit exceeded");
		return -EINVAL;
	}
	val_raw = val / CURRENT_SCALE_FACTOR;

	return regmap_write(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG,
		val_raw);
}

int smb5_set_prop_charging_enabled(struct smb5_chip *chip, unsigned int val)
{
	if (val != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
	    val != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
		return -EINVAL;

	return regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				 CHARGING_ENABLE_CMD_BIT,
				 val == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO ?
				 CHARGING_ENABLE_CMD_BIT : 0);
}

int smb5_inhibit_with_fallback(struct smb5_chip *chip, const char *where)
{
	int rc, suspend_rc;

	rc = smb5_set_prop_charging_enabled(chip,
			POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE);
	if (rc) {
		suspend_rc = regmap_update_bits(chip->regmap,
			chip->base + USBIN_CMD_IL, USBIN_SUSPEND_BIT,
			USBIN_SUSPEND_BIT);
		dev_err(chip->dev,
			"miseed-charge %s stop failed rc=%d input_stop_rc=%d; watchdog not fed\n",
			where, rc, suspend_rc);
	}

	return rc;
}

int smb5_get_iio_chan(struct smb5_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int rc;
	int online;

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to get IIO channel, err = %li\n", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	rc = smb5_get_prop_usb_online(chip, &online);
	if (rc < 0)
		return rc;
	if (!online) {
		*val = 0;
		return 0;
	}

	return iio_read_channel_processed(chan, val);
}

/* ADC5_USB_IN_I reports a voltage in uV, not a current. The locked PM8150B
 * smb5-lib.c specifies 0.2 V/A in buck mode: uV * 5 gives uA. Do not apply
 * that conversion to OTG/boost; this board profile qualifies wired sink only.
 */
int smb5_get_input_current(struct smb5_chip *chip, int *current_ua)
{
	unsigned int otg;
	int online, voltage_uv, ret;

	ret = smb5_get_prop_usb_online(chip, &online);
	if (ret)
		return ret;
	if (!online) {
		*current_ua = 0;
		return 0;
	}
	ret = regmap_read(chip->regmap, chip->base + SMB5_DCDC_CMD_OTG, &otg);
	if (ret)
		return ret;
	if (otg & SMB5_OTG_ENABLE)
		return -ENODATA;
	ret = smb5_get_iio_chan(chip, chip->usb_in_i_chan, &voltage_uv);
	if (ret < 0)
		return ret;
	if (voltage_uv < 0 || voltage_uv > INT_MAX / 5)
		return -ERANGE;
	*current_ua = voltage_uv * 5;
	return 0;
}

int smb5_get_prop_health(struct smb5_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		dev_err(chip->dev, "battery over-voltage\n");
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_7_REG,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status 7 rc=%d\n", rc);
		return rc;
	}

	if (stat & (BAT_TEMP_STATUS_TOO_COLD_BIT | BAT_TEMP_STATUS_TOO_COLD_AFP_BIT))
		*val = POWER_SUPPLY_HEALTH_COLD;
	else if (stat & (BAT_TEMP_STATUS_TOO_HOT_BIT | BAT_TEMP_STATUS_TOO_HOT_AFP_BIT))
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat & BAT_TEMP_STATUS_COLD_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_COOL;
	else if (stat & BAT_TEMP_STATUS_HOT_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_WARM;
	else
		*val = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}


/* PM8150B encodings and watchdog semantics from the device's LOS qpnp-smb5.c.
 * Umi normal FV ceiling is 4.45 V; preserve any lower inherited FV. */
int smb5_init_hw(struct smb5_chip *chip)
{
 unsigned int raw;
 int rc;
 rc = regmap_read(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG, &raw);
 if (rc) return rc;
 chip->max_fv_uv = min(3600000 + (int)raw * 10000, UMI_CHARGE_MAX_UV);
 if (chip->max_fv_uv <= 3600000) return -ERANGE;
 rc = regmap_write(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
                   (chip->max_fv_uv - 3600000) / 10000);
 if (rc) return rc;
	rc = regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
	                  UMI_CHARGE_FCC_UA / CURRENT_SCALE_FACTOR);
 if (rc) return rc;
	rc = smb5_set_current_limit(chip, UMI_CHARGE_USB_ICL_UA);
 if (rc) return rc;
 /* Keep inherited watchdog intervals; enable bite-to-stop and timer. */
 rc = regmap_update_bits(chip->regmap, chip->base + SNARL_BARK_BITE_WD_CFG,
                         BITE_WDOG_DISABLE_CHARGING_CFG_BIT,
                         BITE_WDOG_DISABLE_CHARGING_CFG_BIT);
 if (rc) return rc;
 rc = regmap_update_bits(chip->regmap, chip->base + WD_CFG,
                         WDOG_TIMER_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT,
                         WDOG_TIMER_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT);
 if (rc) return rc;
 /* Retain all other thermal settings, ensure the die shutdown comparator. */
 return regmap_update_bits(chip->regmap, chip->base + 0x670, BIT(0), BIT(0));
}
