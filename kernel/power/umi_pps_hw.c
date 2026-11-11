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

static int umi_pps_property(struct umi_pps *c, const char *name, int *value)
{
	u32 v;
	int ret = device_property_read_u32(c->dev, name, &v);

	if (ret)
		return dev_err_probe(c->dev, ret, "missing qualified board property %s\n", name);
	if (v > INT_MAX)
		return -ERANGE;
	*value = v;
	return 0;
}

static int umi_pps_add_limit(struct umi_pps *c, const char *property,
		int reg, int mask, int base, int step, int min, int max)
{
	int value, encoded, ret = umi_pps_property(c, property, &value);

	if (ret)
		return ret;
	encoded = umi_pps_encode(value, base, step, mask & 0x7f);
	if (value < min || value > max || encoded < 0)
		return dev_err_probe(c->dev, -ERANGE, "invalid exact threshold %s\n", property);
	c->protection[c->nprotection++] = (struct bq25970_reg){reg, mask, encoded};
	return 0;
}

int umi_pps_parse(struct umi_pps *c)
{
	int ret;

	ret = umi_pps_property(c, "xiaomi,thermal-board-compensation-millicelsius",
			       &c->thermal_comp_mc);
	if (ret)
		return ret;
	if (c->thermal_comp_mc != 0)
		return -EINVAL;
	/* Match active LOS Umi configuration: battery OCP and the three BQ TS
	 * comparators are disabled. Thermal/FCC control is supplied by the same
	 * external LOS Umi temperature policy, ported above to mainline APIs.
	 * Do not make parameters mandatory when the reference tree omits them.
	 */
#define LIMIT(name, reg, mask, base, step, min, max) do { \
	ret = umi_pps_add_limit(c, name, reg, mask, base, step, min, max); \
	if (ret) return ret; \
} while (0)
	LIMIT("ti,bat-ovp-millivolt", 0x00, 0xbf, 3475, 25, 4550, 4550);
	LIMIT("ti,bat-ovp-alarm-millivolt", 0x01, 0xff, 3500, 25, 4525, 4525);
	/* LOS explicitly disables the battery OVP alarm while retaining threshold. */
	c->protection[1].value |= 0x80;
	LIMIT("ti,bus-ovp-millivolt", 0x06, 0x7f, 5950, 50, 12000, 12000);
	LIMIT("ti,bus-ovp-alarm-millivolt", 0x07, 0xff, 6000, 50, 11000, 11000);
	LIMIT("ti,bus-ocp-milliamp", 0x08, 0x8f, 1000, 250, 3750, 3750);
	LIMIT("ti,bus-ocp-alarm-milliamp", 0x09, 0xff, 0, 50, 3500, 3500);
	LIMIT("ti,ac-ovp-volt", 0x05, 0x07, 11, 1, 14, 14);
#undef LIMIT
	/* LOS DTS disables battery OCP/OCP alarm and BQ TSBAT/TSBUS/TDIE. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x02, 0x80, 0x80};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x03, 0x80, 0x80};
	/* LOS DTS also disables the battery UCP alarm. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x04, 0x80, 0x80};
	/* Standalone mode, charge off, BQ therm comparators disabled per LOS. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x0c, 0x67, 0x07};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x28, 0xff, 0x15};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x29, 0xff, 0x15};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x2a, 0xff, (1450 - 300) / 5};
	/* Keep the exact register configuration observed on this LOS device. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x2b, 0xf7, 0xd0};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x2c, 0xf0, 0};
	/* LOS init_adc: continuous/averaging/13 bits; IBUS, VBUS, VAC, VBAT.
	 * IBAT is supplied by BMS, not the unqualified pump sense-resistor ADC.
	 */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x14, 0xed, 0xa8};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x15, 0xff, 0x2f};
	/* TI TIDA-01582 disables the watchdog by setting 0x0b[2]. This
	 * policy instead uses the sibling BQ25968-documented 30 s interval;
	 * successful control-loop I2C transactions provide the refresh.
	 */
	c->protection[c->nprotection++] = (struct bq25970_reg){BQ_WATCHDOG,
		BQ_WD_CONFIG_MASK, BQ_WD_ENABLED_30S};
	return 0;
}

int umi_pps_verify(struct umi_pps *c)
{
	return bq25970_verify(c->map, c->protection, c->nprotection);
}

int umi_pps_prepare(struct umi_pps *c)
{
	int ret = bq25970_configure(c->map, c->protection, c->nprotection);
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
	if (s->temp <= -400 || s->temp >= 1250)
		return -ERANGE;
	temp_mc[0] = s->temp * 100;
	for (i = 1; i < UMI_PPS_THERMAL_SENSORS; i++) {
		ret = iio_read_channel_processed(c->thermal_channels[i - 1], &temp_mc[i]);
		if (ret < 0)
			return ret;
	}
	if (umi_pps_now() - start > UMI_PPS_PERIOD_MS)
		return -ETIME;
	/* Until the diagnostic image has a reviewed display-state provider, use
	 * the more restrictive of both LOS screen-dependent battery limits.
	 */
	if (umi_pps_thermal_update(&c->thermal, temp_mc, -1, c->thermal_comp_mc))
		return -ERANGE;
	s->thermal_bat_ma = min(c->thermal.max_bat_ma,
			       umi_pps_cooling_current(c->cooling_state));
	return 0;
}

int umi_pps_adc(struct umi_pps *c, int channel, int *value)
{
	return bq25970_adc(c->map, channel, value);
}

int umi_pps_sample(struct umi_pps *c, struct umi_pps_sample *s)
{
	struct bq25970_status status = {
		.fault = s->fault_raw, .latched = s->latched_raw,
		.alarm = s->alarm_raw, .status = s->status_raw,
		.watchdog = s->watchdog_raw, .control = s->control_raw,
	};
	int ret;

	ret = umi_pps_verify(c);
	/* The watchdog flag is read-to-clear; preserve the value observed by
	 * verification for the normal fault log and safe-stop path.
	 */
	if (ret == -ETIME)
		status.watchdog |= BQ_WD_TIMEOUT_FLAG;
	if (!ret)
		ret = bq25970_fault_snapshot(c->map, &status);
	/* Preserve partial diagnostics if a later read fails after consuming a
	 * read-to-clear latch. The error still forces the normal safe stop.
	 */
	s->fault_raw = status.fault;
	s->latched_raw = status.latched;
	s->alarm_raw = status.alarm;
	s->status_raw = status.status;
	s->watchdog_raw = status.watchdog;
	s->control_raw = status.control;
	if (ret)
		return ret;
	s->fault = s->fault_raw || s->latched_raw || (s->status_raw & 0xcb) ||
		(s->watchdog_raw & BQ_WD_CONFIG_MASK) != BQ_WD_ENABLED_30S ||
		(s->watchdog_raw & BQ_WD_TIMEOUT_FLAG) || !(s->alarm_raw & BIT(1)) ||
		(c->state == PPS_RUN && !(s->control_raw & BQ_ENABLE)) ||
		(c->state == PPS_SETTLE && (s->control_raw & BQ_ENABLE));
	s->alarm = !!(s->alarm_raw & 0xf0);
	s->bus_low = !!(s->status_raw & BIT(5));
	s->bus_high = !!(s->status_raw & BIT(4));
	s->switching = !!(s->status_raw & BIT(2));
	ret = umi_pps_adc(c, 0, &s->ibus_ma);
	if (!ret)
		ret = umi_pps_adc(c, 1, &s->vbus_mv);
	if (!ret)
		ret = umi_pps_adc(c, 4, &s->pump_vbat_mv);
	return ret;
}
