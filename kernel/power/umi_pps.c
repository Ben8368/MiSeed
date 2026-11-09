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
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#include "umi_pps_policy.h"
#include "umi_smb5.h"

/* BQ25970 only. SC8551/NU2105/BQ25968 require distinct ADC/protection ports. */
#define BQ_ID 0x13
#define BQ_CONTROL 0x0c
#define BQ_ENABLE BIT(7)
#define BQ_WATCHDOG 0x0b
#define BQ_WD_DISABLED BIT(2)
#define BQ_ADC 0x14
#define BQ_ADC_BASE 0x16
#define PPS_FIXED 1
#define PPS_PROGRAMMABLE 2

enum umi_pps_state { PPS_OFF, PPS_WAIT_PLUG,
	PPS_SETTLE, PPS_RUN, PPS_DONE, PPS_FAULT };

/* Expected writable protection image, checked during every control cycle. */
struct umi_pps_reg { unsigned int reg, mask, value; };
struct umi_pps {
	struct device *dev;
	struct regmap *map;
	struct power_supply *pd, *bms, *sw;
	struct delayed_work work;
	struct mutex lock;
	struct umi_pps_caps caps;
	struct iio_channel *thermal_channels[UMI_PPS_THERMAL_SENSORS - 1];
	struct umi_pps_thermal thermal;
	int thermal_comp_mc;
	struct umi_pps_reg protection[20];
	unsigned int nprotection;
	enum umi_pps_state state;
	bool claimed, configured, pps, removing, stop_pending, manual_stop;
	u64 deadline, start_deadline, taper_start, last_pps_request_ms;
	int request_mv, request_ma, max_bat_mv;
	int last_error;
};

static u64 umi_pps_now(void) { return ktime_to_ms(ktime_get_boottime()); }

/* I/O may sleep past a worker's operation window. Check at each active
	* operation boundary; stopping/fixed-contract recovery remains unrestricted.
	* This does not bound a hung bus transaction or replace hardware protection.
 */
static int umi_pps_check_deadline(struct umi_pps *c)
{
	u64 now = umi_pps_now();

	if (now >= c->deadline)
		return -ETIME;
	if (c->state == PPS_SETTLE && now >= c->start_deadline)
		return -ETIMEDOUT;
	return 0;
}

static int umi_pps_get(struct power_supply *psy, enum power_supply_property p, int *v)
{
	union power_supply_propval val;
	int ret = power_supply_get_property(psy, p, &val);

	if (!ret)
		*v = val.intval;
	return ret;
}

static int umi_pps_set(struct power_supply *psy, enum power_supply_property p, int v)
{
	union power_supply_propval val = { .intval = v };

	return power_supply_set_property(psy, p, &val);
}

/* TCPM may be servicing a source AMS or discard a colliding request. Retry
 * only that transient result, within the existing operation/startup windows.
 * Transport errors/timeouts still go straight to the stop/recovery path.
 */
static int umi_pps_negotiate(struct umi_pps *c, enum power_supply_property p, int v)
{
	int ret, attempt;

	for (attempt = 0; attempt < 4; attempt++) {
		ret = umi_pps_check_deadline(c);
		if (ret)
			return ret;
		ret = umi_pps_set(c->pd, p, v);
		if (ret != -EAGAIN || attempt == 3)
			return ret;
		ret = umi_pps_check_deadline(c);
		if (ret)
			return ret;
		usleep_range(20000, 25000);
	}
	return -EAGAIN;
}

static int umi_pps_update(struct umi_pps *c, unsigned int reg,
		unsigned int mask, unsigned int value)
{
	unsigned int readback;
	int ret = regmap_update_bits(c->map, reg, mask, value);

	if (!ret)
		ret = regmap_read(c->map, reg, &readback);
	if (!ret && (readback & mask) != value)
		ret = -EIO;
	return ret;
}

static int umi_pps_disable(struct umi_pps *c)
{
	unsigned int status;
	int ret = umi_pps_update(c, BQ_CONTROL, BQ_ENABLE, 0);

	if (!ret)
		ret = regmap_read(c->map, 0x0a, &status);
	if (!ret && (status & BIT(2)))
		ret = -EBUSY;
	return ret;
}

/* Always attempt both stop mechanisms; preserve ownership if either fails. */
static int umi_pps_stop(struct umi_pps *c, enum umi_pps_state state, int reason)
{
	int pump_ret, pd_ret = 0, ret;

	pump_ret = umi_pps_disable(c);
	if (c->pps) {
		int online;

		pd_ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &online);
		if (!pd_ret && online)
			pd_ret = umi_pps_set(c->pd, POWER_SUPPLY_PROP_ONLINE, PPS_FIXED);
		if (!pd_ret)
			pd_ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &online);
		if (!pd_ret && online) {
			int mv, ma;

			pd_ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_VOLTAGE_NOW, &mv);
			if (!pd_ret)
				pd_ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_CURRENT_NOW, &ma);
			if (!pd_ret && (online != PPS_FIXED || mv != 5000000 ||
					ma < 0 || ma > UMI_CHARGE_USB_ICL_UA))
				pd_ret = -EPROTO;
		}
		if (!pd_ret)
			c->pps = false;
	}
	ret = pump_ret ? pump_ret : pd_ret;
	if (!ret && c->claimed) {
		ret = umi_smb5_release(c->sw);
		if (!ret)
			c->claimed = false;
	}
	c->stop_pending = !!ret;
	c->state = ret ? PPS_FAULT : state;
	c->last_error = ret ? ret : reason;
	c->configured = false;
	c->taper_start = 0;
	dev_info(c->dev, "miseed-pps state=%u reason=%d stop=%d pd_stop=%d owned=%u\n",
		 c->state, reason, pump_ret, pd_ret, c->claimed);
	return ret;
}

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
	c->protection[c->nprotection++] = (struct umi_pps_reg){reg, mask, encoded};
	return 0;
}

static int umi_pps_parse(struct umi_pps *c)
{
	int ret;

	ret = umi_pps_property(c, "miseed,thermal-board-compensation-millicelsius",
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
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x02, 0x80, 0x80};
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x03, 0x80, 0x80};
	/* LOS DTS also disables the battery UCP alarm. */
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x04, 0x80, 0x80};
	/* Standalone mode, charge off, BQ therm comparators disabled per LOS. */
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x0c, 0x67, 0x07};
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x28, 0xff, 0x15};
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x29, 0xff, 0x15};
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x2a, 0xff, (1450 - 300) / 5};
	/* Keep the exact register configuration observed on this LOS device. */
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x2b, 0xf7, 0xd0};
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x2c, 0xf0, 0};
	/* LOS init_adc: continuous/averaging/13 bits; IBUS, VBUS, VAC, VBAT.
	 * IBAT is supplied by BMS, not the unqualified pump sense-resistor ADC.
	 */
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x14, 0xed, 0xa8};
	c->protection[c->nprotection++] = (struct umi_pps_reg){0x15, 0xff, 0x2f};
	/* Match fixed LOS init_device: enable_wdt(false). Do not change timing. */
	c->protection[c->nprotection++] = (struct umi_pps_reg){BQ_WATCHDOG,
		BQ_WD_DISABLED, BQ_WD_DISABLED};
	return 0;
}

static int umi_pps_verify(struct umi_pps *c)
{
	unsigned int i, v;
	int ret;

	for (i = 0; i < c->nprotection; i++) {
		struct umi_pps_reg *r = &c->protection[i];

		ret = regmap_read(c->map, r->reg, &v);
		if (ret)
			return ret;
		if ((v & r->mask) != r->value)
			return -EPERM;
		/* Native Android 0x2b was 0xd0. Preserve and require its VOUT
		 * control bit; do not guess the contradictory header's polarity.
		 */
		if (r->reg == 0x2b && (v & BIT(3)))
			return -EPERM;
		if ((r->reg == 0x05 && (v & 0xc0)) || (r->reg == 0x2c && (v & 3)))
			return -EIO;
	}
	return 0;
}

static int umi_pps_prepare(struct umi_pps *c)
{
	unsigned int i;
	int ret = umi_pps_disable(c);

	for (i = 0; !ret && i < c->nprotection; i++) {
		struct umi_pps_reg *r = &c->protection[i];

		ret = umi_pps_update(c, r->reg, r->mask, r->value);
	}
	if (!ret)
		ret = umi_pps_verify(c);
	c->configured = !ret;
	return ret;
}

static int umi_pps_battery(struct umi_pps *c, struct umi_pps_sample *s)
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
		/* This pinned FG reports charging as negative; divide before negating. */
		if (!ret)
			s->ibat_ma = -(raw / 1000);
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
	s->thermal_bat_ma = c->thermal.max_bat_ma;
	return 0;
}

static int umi_pps_adc(struct umi_pps *c, int channel, int *value)
{
	u8 bytes[2];
	int ret = regmap_bulk_read(c->map, BQ_ADC_BASE + 2 * channel, bytes, 2);

	if (!ret)
		*value = (s16)((bytes[0] << 8) | bytes[1]);
	return ret;
}

static int umi_pps_sample(struct umi_pps *c, struct umi_pps_sample *s)
{
	int ret;

	ret = umi_pps_verify(c);
	if (!ret)
		ret = regmap_read(c->map, 0x10, &s->fault_raw);
	if (!ret)
		ret = regmap_read(c->map, 0x11, &s->latched_raw);
	if (!ret)
		ret = regmap_read(c->map, 0x0d, &s->alarm_raw);
	if (!ret)
		ret = regmap_read(c->map, 0x0a, &s->status_raw);
	if (!ret)
		ret = regmap_read(c->map, BQ_WATCHDOG, &s->watchdog_raw);
	if (!ret)
		ret = regmap_read(c->map, BQ_CONTROL, &s->control_raw);
	if (ret)
		return ret;
	s->fault = s->fault_raw || s->latched_raw || (s->status_raw & 0xcb) ||
		!(s->watchdog_raw & BQ_WD_DISABLED) || !(s->alarm_raw & BIT(1)) ||
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

static int umi_pps_caps(struct umi_pps *c)
{
	int ret, v;

	ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_USB_TYPE, &v);
	if (!ret && v != POWER_SUPPLY_USB_TYPE_PD_PPS)
		ret = -EOPNOTSUPP;
	if (!ret) {
		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_VOLTAGE_MIN, &v);
		if (!ret) c->caps.min_mv = v / 1000;
	}
	if (!ret) {
		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_VOLTAGE_MAX, &v);
		if (!ret) c->caps.max_mv = v / 1000;
	}
	if (!ret) {
		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_CURRENT_MAX, &v);
		if (!ret) c->caps.max_ma = v / 1000;
	}
	if (!ret && !umi_pps_caps_ok(&c->caps))
		ret = -ERANGE;
	return ret;
}

static int umi_pps_contract(struct umi_pps *c)
{
	int online, v, a, ret;

	ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &online);
	if (!ret)
		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_VOLTAGE_NOW, &v);
	if (!ret)
		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_CURRENT_NOW, &a);
	if (!ret && (online != PPS_PROGRAMMABLE || v != c->request_mv * 1000 ||
		     a != c->request_ma * 1000))
		ret = -EPROTO;
	return ret;
}

static int umi_pps_request(struct umi_pps *c, int mv)
{
	int ret = 0, ma;
	u64 now = umi_pps_now();
	bool request = mv != c->request_mv || !c->last_pps_request_ms ||
		now - c->last_pps_request_ms >= UMI_PPS_REFRESH_MS;

	if (mv < c->caps.min_mv || mv > min(c->caps.max_mv, UMI_PPS_MAX_MV) ||
	    mv % UMI_PPS_STEP_MV || c->request_ma > c->caps.max_ma)
		return -ERANGE;
	ret = umi_pps_check_deadline(c);
	if (ret)
		return ret;
	if (request) {
		ret = umi_pps_negotiate(c, POWER_SUPPLY_PROP_VOLTAGE_NOW, mv * 1000);
		if (!ret)
			ret = umi_pps_check_deadline(c);
		if (!ret)
			ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_CURRENT_NOW, &ma);
		/* A voltage request carries the negotiated current too. */
		if (!ret && ma != c->request_ma * 1000)
			ret = umi_pps_negotiate(c, POWER_SUPPLY_PROP_CURRENT_NOW,
						c->request_ma * 1000);
		if (!ret) {
			c->request_mv = mv;
			c->last_pps_request_ms = umi_pps_now();
		}
	}
	if (!ret)
		ret = umi_pps_contract(c);
	if (!ret)
		ret = umi_pps_check_deadline(c);
	return ret;
}

static int umi_pps_begin(struct umi_pps *c, struct umi_pps_sample *s)
{
	int ret, mv;

	ret = umi_smb5_claim(c->sw);
	if (ret == -EBUSY || ret == -ENODEV)
		return ret;
	c->claimed = true;
	if (ret)
		return ret;
	ret = umi_smb5_check(c->sw);
	if (ret < 0)
		return ret;
	c->max_bat_mv = ret / 1000;
	if (!umi_pps_battery_ok(s, c->max_bat_mv))
		return -ERANGE;
	ret = umi_pps_prepare(c);
	if (ret)
		return ret;
	ret = umi_pps_check_deadline(c);
	if (ret)
		return ret;
	/* A failed negotiation may still have changed the source. Always unwind. */
	c->pps = true;
	ret = umi_pps_negotiate(c, POWER_SUPPLY_PROP_ONLINE, PPS_PROGRAMMABLE);
	/* TCPM returns -EOPNOTSUPP before changing the contract when no PPS APDO
	 * was advertised. In that case there is no PPS contract to unwind, and a
	 * second ONLINE write can itself fail and strand SMB5 ownership.
	 */
	if (ret == -EOPNOTSUPP)
		c->pps = false;
	if (!ret)
		ret = umi_pps_check_deadline(c);
	if (!ret)
		ret = umi_pps_caps(c);
	if (ret)
		return ret;
	mv = umi_pps_target(&c->caps, s->vbat_mv);
	c->request_ma = umi_pps_current(&c->caps,
		min(UMI_PPS_MAX_MA, s->thermal_bat_ma / 2));
	if (mv < 0 || c->request_ma < 0)
		return -ERANGE;
	/* Start a fresh keep-alive window for every PPS contract. */
	c->last_pps_request_ms = 0;
	ret = umi_pps_request(c, mv);
	if (!ret) {
		c->start_deadline = umi_pps_now() + UMI_PPS_START_MS;
		c->state = PPS_SETTLE;
		dev_info(c->dev, "miseed-pps settle request_mv=%d request_ma=%d apdo_min_mv=%d apdo_max_mv=%d apdo_max_ma=%d\n",
			 c->request_mv, c->request_ma, c->caps.min_mv, c->caps.max_mv, c->caps.max_ma);
	}
	return ret;
}

static int umi_pps_validate_cycle(struct umi_pps *c)
{
	int ret = umi_pps_check_deadline(c);

	/* Readback only: LOS disables the pump watchdog; no periodic keepalive. */
	if (!ret)
		ret = umi_pps_verify(c);
	if (!ret)
		ret = umi_pps_check_deadline(c);
	return ret;
}

static void umi_pps_work(struct work_struct *work)
{
	struct umi_pps *c = container_of(to_delayed_work(work), struct umi_pps, work);
	struct umi_pps_sample s = {};
	u64 now;
	int ret = 0, online, next;

	mutex_lock(&c->lock);
	if (c->removing)
		goto out;
	if (c->stop_pending) {
		umi_pps_stop(c, PPS_FAULT, c->last_error);
		goto out;
	}
	ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &online);
	if (ret)
		goto fail;
	if (!online) {
		if (c->state == PPS_SETTLE || c->state == PPS_RUN || c->pps || c->claimed)
			umi_pps_stop(c, PPS_WAIT_PLUG, 0);
		else {
			c->state = PPS_WAIT_PLUG;
			c->manual_stop = false;
		}
		goto out;
	}
	if (c->manual_stop || c->state == PPS_DONE || c->state == PPS_FAULT ||
	    c->state == PPS_OFF)
		goto out;
	if (c->state == PPS_WAIT_PLUG) {
		int usb_type;

		ret = umi_pps_get(c->pd, POWER_SUPPLY_PROP_USB_TYPE, &usb_type);
		if (ret)
			goto fail;
		/* Fixed-PD is the expected starting state: ONLINE=PPS_PROGRAMMABLE
		 * asks TCPM to select an advertised APDO. Ordinary USB stays on SMB5.
		 */
		if (usb_type != POWER_SUPPLY_USB_TYPE_PD &&
		    usb_type != POWER_SUPPLY_USB_TYPE_PD_PPS)
			goto out;
	}
	ret = umi_pps_battery(c, &s);
	if (ret)
		goto fail;
	if (c->state == PPS_WAIT_PLUG) {
		/* Wait without claiming SMB5 until the LOS PPS entry conditions hold. */
		if (!umi_pps_battery_ok(&s, UMI_CHARGE_MAX_UV / 1000))
			goto out;
		c->deadline = umi_pps_now() + UMI_PPS_OPERATION_MS;
		ret = umi_pps_begin(c, &s);
		if (ret)
			goto fail;
		goto out;
	}
	now = umi_pps_now();
	c->deadline = now + UMI_PPS_OPERATION_MS;
	ret = umi_smb5_check(c->sw);
	if (ret < 0)
		goto fail;
	if (ret / 1000 != c->max_bat_mv) {
		ret = -EPERM;
		goto fail;
	}
	ret = umi_pps_contract(c);
	if (!ret)
		ret = umi_pps_caps(c);
	if (!ret && (c->request_mv < c->caps.min_mv || c->request_mv > c->caps.max_mv ||
		     c->request_ma > c->caps.max_ma))
		ret = -ERANGE;
	if (!ret)
		ret = umi_pps_sample(c, &s);
	if (ret)
		goto fail;
	if (s.soc >= UMI_PPS_MAX_SOC || s.vbat_mv >= c->max_bat_mv) {
		umi_pps_stop(c, PPS_DONE, 0);
		goto out;
	}
	if (!umi_pps_sample_ok(&s, c->max_bat_mv, c->request_ma,
			       c->request_mv, c->state == PPS_SETTLE)) {
		/* Independent BMS/pump conversions may still reflect the previous
		 * charging path. Wait with the pump OFF for voltage agreement.
		 * Absolute limits, faults and the original 5s deadline still apply;
		 * never enable with a mismatch or wait through an absolute fault.
		 */
		if (c->state == PPS_SETTLE && s.vbus_mv <= c->request_mv + 200 &&
		    umi_pps_sample_limits(&s, c->max_bat_mv, c->request_ma)) {
			ret = umi_pps_check_deadline(c);
			if (ret)
				goto fail;
			dev_info(c->dev, "miseed-pps waiting for ADC vbus_mv=%d request_mv=%d vbat_mv=%d pump_vbat_mv=%d\n",
				 s.vbus_mv, c->request_mv, s.vbat_mv, s.pump_vbat_mv);
			goto out;
		}
		ret = -ERANGE;
		goto fail;
	}
	/* Tighten the adapter's current before any further voltage request.
	 * A session never raises this current again after a thermal reduction.
	 */
	next = umi_pps_current(&c->caps, min(UMI_PPS_MAX_MA, s.thermal_bat_ma / 2));
	if (next < 0) {
		ret = -ERANGE;
		goto fail;
	}
	if (next < c->request_ma) {
		ret = umi_pps_check_deadline(c);
		if (!ret)
			ret = umi_pps_negotiate(c, POWER_SUPPLY_PROP_CURRENT_NOW, next * 1000);
		if (!ret) {
			c->request_ma = next;
			c->last_pps_request_ms = umi_pps_now();
			ret = umi_pps_contract(c);
		}
		if (!ret)
			ret = umi_pps_check_deadline(c);
		if (ret)
			goto fail;
	}
	if (c->state == PPS_SETTLE) {
		if (now >= c->start_deadline) {
			ret = -ETIMEDOUT;
			goto fail;
		}
		if (!s.bus_low && !s.bus_high && !s.alarm) {
			/* All gates reread after negotiation; enable is the last operation. */
			ret = umi_pps_validate_cycle(c);
			if (!ret)
				ret = umi_pps_check_deadline(c);
			if (!ret)
				ret = umi_pps_update(c, BQ_CONTROL, BQ_ENABLE, BQ_ENABLE);
			if (!ret)
				ret = umi_pps_check_deadline(c);
			if (ret)
				goto fail;
			c->state = PPS_RUN;
			dev_info(c->dev, "miseed-pps pump enabled request_mv=%d request_ma=%d\n",
				 c->request_mv, c->request_ma);
			goto out;
		}
	} else {
		if (!s.switching || s.bus_high || s.bus_low) {
			ret = -EIO;
			goto fail;
		}
		if (umi_pps_control_vbat(&s) >= c->max_bat_mv - 80 &&
		    s.ibat_ma < UMI_PPS_TAPER_MA) {
			if (!c->taper_start)
				c->taper_start = now;
			if (now - c->taper_start >= UMI_PPS_TAPER_MS) {
				umi_pps_stop(c, PPS_DONE, 0);
				goto out;
			}
		} else {
			c->taper_start = 0;
		}
	}
	next = umi_pps_next_mv(&s, &c->caps, c->max_bat_mv, c->request_ma, c->request_mv);
	if (next < 0) {
		ret = -ERANGE;
		goto fail;
	}
	ret = umi_pps_request(c, next);
	/* A PD request can sleep. Validate fresh telemetry after it returns. */
	if (!ret)
		ret = umi_pps_battery(c, &s);
	if (!ret)
		ret = umi_pps_sample(c, &s);
	if (!ret && (!umi_pps_sample_ok(&s, c->max_bat_mv, c->request_ma,
			       c->request_mv, c->state == PPS_SETTLE) ||
		     (c->state == PPS_RUN && (!s.switching || s.bus_low || s.bus_high))))
		ret = -ERANGE;
	if (!ret)
		ret = umi_pps_validate_cycle(c);
	if (ret)
		goto fail;
	dev_info(c->dev, "miseed-pps state=%u vbus_mv=%d ibus_ma=%d vbat_mv=%d pump_vbat_mv=%d ibat_ma=%d request_mv=%d request_ma=%d thermal_mc=%d thermal_level=%d thermal_bat_ma=%d\n",
		 c->state, s.vbus_mv, s.ibus_ma, s.vbat_mv, s.pump_vbat_mv, s.ibat_ma,
		 c->request_mv, c->request_ma,
		 c->thermal.virtual_mc, c->thermal.level, s.thermal_bat_ma);
	goto out;
fail:
	dev_info(c->dev, "miseed-pps failure state=%u error=%d request_mv=%d request_ma=%d max_bat_mv=%d vbus_mv=%d ibus_ma=%d vbat_mv=%d pump_vbat_mv=%d ibat_ma=%d fault=%d alarm=%d bus_low=%d bus_high=%d switching=%d bq_fault=0x%02x bq_latched=0x%02x bq_alarm=0x%02x bq_status=0x%02x bq_watchdog=0x%02x bq_control=0x%02x\n",
		 c->state, ret, c->request_mv, c->request_ma, c->max_bat_mv,
		 s.vbus_mv, s.ibus_ma, s.vbat_mv, s.pump_vbat_mv,
		 s.ibat_ma, s.fault, s.alarm, s.bus_low, s.bus_high, s.switching,
		 s.fault_raw, s.latched_raw, s.alarm_raw, s.status_raw,
		 s.watchdog_raw, s.control_raw);
	if (c->state == PPS_WAIT_PLUG && !c->claimed && !c->pps) {
		/* Pre-negotiation telemetry faults are transient; keep polling without
		 * taking ownership from the main charger.
		 */
		c->last_error = ret;
		goto out;
	}
	umi_pps_stop(c, PPS_FAULT, ret);
out:
	if (!c->removing && (c->stop_pending || !c->manual_stop))
		mod_delayed_work(system_wq, &c->work, msecs_to_jiffies(UMI_PPS_PERIOD_MS));
	mutex_unlock(&c->lock);
}

static irqreturn_t umi_pps_irq(int irq, void *data)
{
	struct umi_pps *c = data;

	/* Fault flags are collected by the same serialized reader as polling. */
	mutex_lock(&c->lock);
	if (!c->removing)
		mod_delayed_work(system_wq, &c->work, 0);
	mutex_unlock(&c->lock);
	return IRQ_HANDLED;
}

static ssize_t fastcharge_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct umi_pps *c = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&c->lock);
	ret = sysfs_emit(buf, "state=%u error=%d requested_mv=%d requested_ma=%d stop_pending=%u\n",
		c->state, c->last_error, c->request_mv, c->request_ma, c->stop_pending);
	mutex_unlock(&c->lock);
	return ret;
}

static ssize_t fastcharge_test_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct umi_pps *c = dev_get_drvdata(dev);
	int ret;

	if (!sysfs_streq(buf, "stop"))
		return -EINVAL;
	mutex_lock(&c->lock);
	if (c->removing) {
		ret = -ENODEV;
		goto out;
	}
	ret = umi_pps_stop(c, PPS_OFF, 0);
	c->manual_stop = true;
	mod_delayed_work(system_wq, &c->work, 0);
out:
	mutex_unlock(&c->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(fastcharge_test);
static struct attribute *umi_pps_attrs[] = { &dev_attr_fastcharge_test.attr, NULL };
static const struct attribute_group umi_pps_group = { .attrs = umi_pps_attrs };

static void umi_pps_cleanup(void *data)
{
	struct umi_pps *c = data;

	mutex_lock(&c->lock);
	c->removing = true;
	mutex_unlock(&c->lock);
	cancel_delayed_work_sync(&c->work);
	mutex_lock(&c->lock);
	umi_pps_stop(c, PPS_OFF, 0);
	mutex_unlock(&c->lock);
}

static int umi_pps_suspend(struct device *dev)
{
	struct umi_pps *c = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&c->lock);
	ret = umi_pps_stop(c, PPS_OFF, 0);
	mutex_unlock(&c->lock);
	/* Refuse suspend if power could not be made safe; retry while awake. */
	if (ret)
		mod_delayed_work(system_wq, &c->work, 0);
	return ret;
}
static DEFINE_SIMPLE_DEV_PM_OPS(umi_pps_pm, umi_pps_suspend, NULL);

static const struct regmap_config umi_pps_regmap = {
	.reg_bits = 8, .val_bits = 8, .max_register = 0x2e,
	.cache_type = REGCACHE_NONE,
};

static int umi_pps_probe(struct i2c_client *client)
{
	static const char * const thermal_names[] = {
		"cam_therm1", "conn_therm", "quiet_therm", "wireless_therm", "xo_therm"
	};
	struct device *dev = &client->dev;
	struct umi_pps *c;
	unsigned int id, mode;
	int ret, i;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = dev;
	mutex_init(&c->lock);
	INIT_DELAYED_WORK(&c->work, umi_pps_work);
	i2c_set_clientdata(client, c);
	c->map = devm_regmap_init_i2c(client, &umi_pps_regmap);
	if (IS_ERR(c->map))
		return PTR_ERR(c->map);
	ret = regmap_read(c->map, BQ_ID, &id);
	if (ret)
		return ret;
	if (id != 0x10)
		return dev_err_probe(dev, -ENODEV, "unqualified pump ID 0x%x (no clone fallback)\n", id);
	ret = regmap_read(c->map, BQ_CONTROL, &mode);
	if (ret)
		return ret;
	if (mode & 0x60)
		return dev_err_probe(dev, -EINVAL, "only standalone BQ25970 is qualified by this port\n");
	ret = umi_pps_disable(c);
	if (ret)
		return ret;
	ret = umi_pps_parse(c);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(thermal_names); i++) {
		enum iio_chan_type type;

		c->thermal_channels[i] = devm_iio_channel_get(dev, thermal_names[i]);
		if (IS_ERR(c->thermal_channels[i]))
			return dev_err_probe(dev, PTR_ERR(c->thermal_channels[i]),
					     "missing thermal channel %s\n", thermal_names[i]);
		ret = iio_get_channel_type(c->thermal_channels[i], &type);
		if (ret || type != IIO_TEMP)
			return dev_err_probe(dev, ret ? ret : -EINVAL,
					     "thermal channel must report temperature\n");
	}
	c->sw = devm_power_supply_get_by_reference(dev, "miseed,switch-charger");
	if (IS_ERR(c->sw))
		return PTR_ERR(c->sw);
	if (!c->sw)
		return -EPROBE_DEFER;
	c->bms = devm_power_supply_get_by_reference(dev, "monitored-battery");
	if (IS_ERR(c->bms))
		return PTR_ERR(c->bms);
	if (!c->bms)
		return -EPROBE_DEFER;
	c->pd = devm_power_supply_get_by_reference(dev, "miseed,pd-source");
	if (IS_ERR(c->pd))
		return PTR_ERR(c->pd);
	if (!c->pd)
		return -EPROBE_DEFER;
	/* Unbind the consumer before any supply driver releases its private data. */
	if (!device_link_add(dev, c->sw->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER) ||
	    !device_link_add(dev, c->bms->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER) ||
	    !device_link_add(dev, c->pd->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENOMEM;
	ret = devm_add_action_or_reset(dev, umi_pps_cleanup, c);
	if (ret)
		return ret;
	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL, "qualified fault IRQ is required\n");
	ret = devm_request_threaded_irq(dev, client->irq, NULL, umi_pps_irq,
			IRQF_ONESHOT, dev_name(dev), c);
	if (ret)
		return ret;
	ret = devm_device_add_group(dev, &umi_pps_group);
	if (!ret) {
		c->state = PPS_WAIT_PLUG;
		dev_info(dev, "automatic PPS policy active; waiting for USB-PD attach\n");
		mod_delayed_work(system_wq, &c->work, 0);
	}
	return ret;
}

static void umi_pps_shutdown(struct i2c_client *client)
{
	umi_pps_cleanup(i2c_get_clientdata(client));
}

static const struct of_device_id umi_pps_match[] = {
	{ .compatible = "miseed,umi-bq25970-pps" }, {}
};
MODULE_DEVICE_TABLE(of, umi_pps_match);
static struct i2c_driver umi_pps_driver = {
	.driver = { .name = "umi-bq25970-pps", .of_match_table = umi_pps_match,
		.pm = pm_sleep_ptr(&umi_pps_pm) },
	.probe = umi_pps_probe, .shutdown = umi_pps_shutdown,
};
module_i2c_driver(umi_pps_driver);
MODULE_AUTHOR("Texas Instruments; MiSeed contributors");
MODULE_DESCRIPTION("Umi guarded BQ25970/mainline TCPM PPS charging");
MODULE_LICENSE("GPL");
