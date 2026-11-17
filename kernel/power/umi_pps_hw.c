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

int umi_pps_parse(struct umi_pps *c)
{
	u32 compensation;
	int ret = device_property_read_u32(c->dev,
		"xiaomi,thermal-board-compensation-millicelsius", &compensation);

	if (ret)
		return ret;
	if (compensation)
		return -EINVAL;
	c->thermal_comp_mc = 0;
	return 0;
}

int umi_pps_prepare(struct umi_pps *c)
{
	int ret = bq25970_prepare(c->chip);

	c->configured = !ret;
	return ret;
}

int umi_pps_battery(struct umi_pps *c, struct umi_pps_sample *s)
{
	int ret, raw, i, temp_mc[UMI_PPS_THERMAL_SENSORS];
	u64 start = umi_pps_now();

	ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &s->online);
	if (!ret)
		ret = umi_pps_get(c->bms, POWER_SUPPLY_PROP_PRESENT, &s->present);
	if (!ret)
		ret = umi_pps_get(c->bms, POWER_SUPPLY_PROP_TEMP, &s->temp);
	if (!ret)
		ret = umi_pps_get(c->bms, POWER_SUPPLY_PROP_CAPACITY, &s->soc);
	if (!ret) {
		ret = umi_pps_get(c->sw, POWER_SUPPLY_PROP_HEALTH, &raw);
		s->good = !ret && raw == POWER_SUPPLY_HEALTH_GOOD;
	}
	if (!ret) {
		ret = umi_pps_get(c->bms, POWER_SUPPLY_PROP_VOLTAGE_NOW, &raw);
		if (!ret)
			s->vbat_mv = raw / 1000;
	}
	if (!ret) {
		ret = umi_pps_get(c->bms, POWER_SUPPLY_PROP_CURRENT_NOW, &raw);
		/* Standard battery CURRENT_NOW is positive while charging. */
		if (!ret)
			s->ibat_ma = raw / 1000;
	}
	if (ret)
		return ret;
	/* Validate before the deci-C -> milli-C multiplication. */
	if (s->temp <= -400 || s->temp >= 1250) {
		dev_err(c->dev, "miseed-pps battery temperature out of range temp_decic=%d\n",
			s->temp);
		return -ERANGE;
	}
	temp_mc[0] = s->temp * 100;
	for (i = 1; i < UMI_PPS_THERMAL_SENSORS; i++) {
		ret = iio_read_channel_processed(c->thermal_channels[i - 1], &temp_mc[i]);
		if (ret < 0) {
			dev_err(c->dev, "miseed-pps thermal sensor read failed index=%d error=%d battery_mc=%d\n",
				i, ret, temp_mc[0]);
			return ret;
		}
	}
	if (umi_pps_now() - start > UMI_PPS_PERIOD_MS)
		return -ETIME;
	/* Until the diagnostic image has a reviewed display-state provider, use
	 * the more restrictive of both LOS screen-dependent battery limits.
	 */
	if (umi_pps_thermal_update(&c->thermal, temp_mc, -1, c->thermal_comp_mc)) {
		dev_err(c->dev, "miseed-pps thermal sample rejected mc=%d,%d,%d,%d,%d,%d step=%d board_comp_mc=%d\n",
			temp_mc[0], temp_mc[1], temp_mc[2], temp_mc[3],
			temp_mc[4], temp_mc[5], c->thermal.step,
			c->thermal_comp_mc);
		return -ERANGE;
	}
	s->thermal_bat_ma = min(c->thermal.max_bat_ma,
			       umi_pps_cooling_current(c->cooling_state));
	s->control_bat_ma = min(c->thermal.control_bat_ma, s->thermal_bat_ma);
	return 0;
}

int umi_pps_sample(struct umi_pps *c, struct umi_pps_sample *s)
{
	struct bq25970_sample sample = {};
	int ret = bq25970_read_sample(c->chip, &sample);

	/* Raw bytes are diagnostics only. The chip owns their interpretation. */
	s->fault_raw = sample.fault;
	s->latched_raw = sample.latched;
	s->alarm_raw = sample.alarm;
	s->status_raw = sample.status;
	s->watchdog_raw = sample.watchdog;
	s->control_raw = sample.control;
	if (ret)
		return ret;
	s->fault = sample.hardware_fault || !sample.input_present ||
		(c->state == PPS_RUN && !sample.enabled) ||
		((c->state == PPS_SETTLE || c->state == PPS_THERMAL) && sample.enabled);
	s->alarm = sample.alarm_active;
	s->bus_low = sample.bus_low;
	s->bus_high = sample.bus_high;
	s->switching = sample.switching;
	s->clean_disabled = sample.clean_disabled;
	s->ibus_ma = sample.ibus_ua / 1000;
	s->vbus_mv = sample.vbus_uv / 1000;
	s->pump_vbat_mv = sample.vbat_uv / 1000;
	return 0;
}
