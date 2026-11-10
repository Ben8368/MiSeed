// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#include "qcom_fg_internal.h"

/*************************
 * BATTERY STATUS
 * ***********************/

/**
 * @brief qcom_fg_get_capacity() - Get remaining capacity of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_get_capacity(struct qcom_fg_chip *chip, int *val)
{
	u8 cap[2];
	int ret;

	ret = qcom_fg_read(chip, cap, BATT_MONOTONIC_SOC, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read capacity: %d", ret);
		return ret;
	}

	if (cap[0] != cap[1]) {
		cap[0] = cap[0] < cap[1] ? cap[0] : cap[1];
	}

	if (cap[0] == 0 || cap[0] == 0xff)
		*val = cap[0] ? 100 : 0;
	else
		*val = DIV_ROUND_CLOSEST((cap[0] - 1) * 98, 0xff - 2) + 1;

	return 0;
}

/**
 * @brief qcom_fg_get_temperature() - Get temperature of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_get_temperature(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_sram_read(chip, readval, BATT_TEMP, 2, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read temperature: %d", ret);
		return ret;
	}

	temp = readval[1] << 8 | readval[0];
	*val = temp * 625 / 1000 - 2730;
	return 0;
}

/**
 * @brief qcom_fg_get_current() - Get current being drawn from battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_get_current(struct qcom_fg_chip *chip, int *val)
{
	s16 temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_sram_read(chip, readval, BATT_VOLTAGE_CURRENT, 2, 3);
	if (ret) {
		dev_err(chip->dev, "Failed to read current: %d", ret);
		return ret;
	}

	temp = (s16)(readval[1] << 8 | readval[0]);
	*val = div_s64((s64)temp * 152587, 1000);

	return 0;
}

/**
 * @brief qcom_fg_get_voltage() - Get voltage of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_get_voltage(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_sram_read(chip, readval, BATT_VOLTAGE_CURRENT, 2, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to read voltage: %d", ret);
		return ret;
	}

	temp = readval[1] << 8 | readval[0];
	*val = div_u64((u64)temp * 152587, 1000);

	return 0;
}

/**
 * @brief qcom_fg_get_temp_threshold() - Get configured temperature thresholds
 *
 * @param chip Pointer to chip
 * @param psp Power supply property of temperature limit
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_get_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int *val)
{
	u8 temp;
	int offset;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_MIN:
		offset = 2;
		break;
	case POWER_SUPPLY_PROP_TEMP_MAX:
		offset = 3;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		offset = 0;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		offset = 1;
		break;
	default:
		return -EINVAL;
	}

	ret = qcom_fg_sram_read(chip, &temp, TEMP_THRESHOLD, 1, offset);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read JEITA property %d level: %d\n", psp, ret);
		return ret;
	}

	*val = (temp - 30) * 10;

	return 0;
}

/**
 * @brief qcom_fg_set_temp_threshold() - Configure temperature thresholds
 *
 * @param chip Pointer to chip
 * @param psp Power supply property of temperature limit
 * @param val Pointer to get value from
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_set_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int val)
{
	u8 temp;
	int offset;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_MIN:
		offset = 2;
		break;
	case POWER_SUPPLY_PROP_TEMP_MAX:
		offset = 3;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		offset = 0;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		offset = 1;
		break;
	default:
		return -EINVAL;
	}

	temp = val / 10 + 30;

	ret = qcom_fg_sram_write(chip, &temp, TEMP_THRESHOLD, 1, offset);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write JEITA property %d level: %d\n", psp, ret);
		return ret;
	}

	return 0;
}

/*************************
 * BATTERY STATUS, GEN3
 * ***********************/

/**
 * @brief qcom_fg_gen3_get_temperature() - Get temperature of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_gen3_get_temperature(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, PARAM_ADDR_BATT_TEMP, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read temperature: %d\n", ret);
		return ret;
	}

	temp = ((readval[1] & BATT_TEMP_MSB_MASK) << 8) |
		(readval[0] & BATT_TEMP_LSB_MASK);
	temp = DIV_ROUND_CLOSEST(temp * 10, 4);

	*val = temp -2730;
	return 0;
}

/**
 * @brief qcom_fg_gen3_get_current() - Get current being drawn from battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_gen3_get_current(struct qcom_fg_chip *chip, int *val)
{
	s16 temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, PARAM_ADDR_BATT_CURRENT, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read current: %d\n", ret);
		return ret;
	}

	//handle rev 1 too
	temp = (s16)(readval[1] << 8 | readval[0]);
	*val = div_s64((s64)temp * 488281, 1000);

	return 0;
}

/**
 * @brief qcom_fg_gen3_get_voltage() - Get voltage of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_gen3_get_voltage(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, PARAM_ADDR_BATT_VOLTAGE, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read voltage: %d\n", ret);
		return ret;
	}

	//handle rev 1 too
	temp = readval[1] << 8 | readval[0];
	*val = div_u64((u64)temp * 122070, 1000);
	return 0;
}

/**
 * @brief qcom_fg_gen3_get_temp_threshold() - Get configured temperature thresholds
 *
 * @param chip Pointer to chip
 * @param psp Power supply property of temperature limit
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_gen3_get_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int *val)
{
	u8 temp;
	u16 reg;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_MIN:
		reg = BATT_INFO_JEITA_COLD;
		break;
	case POWER_SUPPLY_PROP_TEMP_MAX:
		reg = BATT_INFO_JEITA_HOT;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		reg = BATT_INFO_JEITA_COOL;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		reg = BATT_INFO_JEITA_WARM;
		break;
	default:
		return -EINVAL;
	}

	ret = qcom_fg_read(chip, &temp, reg, 1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read JEITA property %d level: %d\n", psp, ret);
		return ret;
	}

	/* Resolution is 0.5C. Base is -30C. */
	*val = 5 * (int)temp - 300;
	return 0;
}

/*************************
 * BATTERY STATUS, GEN4
 * ***********************/

/**
 * @brief qcom_fg_gen4_get_temperature() - Get temperature of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_gen4_get_temperature(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, ADC_RR_BATT_TEMP_LSB, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read temperature: %d", ret);
		return ret;
	}

	temp = readval[1] << 8 | readval[0];
	*val = temp * 10;
	return 0;
}
