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

u64 umi_pps_now(void) { return ktime_to_ms(ktime_get_boottime()); }

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

int umi_pps_get(struct power_supply *psy, enum power_supply_property p, int *v)
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

int umi_pps_update(struct umi_pps *c, unsigned int reg,
		unsigned int mask, unsigned int value)
{
	return bq25970_update(c->map, reg, mask, value);
}

int umi_pps_disable(struct umi_pps *c)
{
	return bq25970_disable(c->map);
}

/* Always attempt both stop mechanisms; preserve ownership if either fails. */
int umi_pps_stop(struct umi_pps *c, enum umi_pps_state state, int reason)
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
	umi_pps_power_update(&c->power, s, c->thermal.virtual_mc, umi_pps_now());
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

	/* Each successful watchdog-register read refreshes the 30 s window. */
	if (!ret)
		ret = umi_pps_verify(c);
	if (!ret)
		ret = umi_pps_check_deadline(c);
	return ret;
}

/* Only the clean loss of RUN's enable/switching bits can be a detach race.
 * Absolute limits, sensor errors and real/latched protections remain faults.
 */
static bool umi_pps_pump_lost(struct umi_pps *c, struct umi_pps_sample *s)
{
	struct umi_pps_sample live = *s;

	if (c->state != PPS_RUN || !s->fault || s->fault_raw || s->latched_raw ||
	    s->status_raw || s->alarm_raw != BIT(1) ||
	    (s->watchdog_raw & BQ_WD_CONFIG_MASK) != BQ_WD_ENABLED_30S ||
	    (s->watchdog_raw & BQ_WD_TIMEOUT_FLAG) || (s->control_raw & BQ_ENABLE))
		return false;
	live.online = 1;
	live.fault = 0;
	return umi_pps_sample_ok(&live, c->max_bat_mv, c->request_ma,
				 c->request_mv, false);
}

void umi_pps_work(struct work_struct *work)
{
	struct umi_pps *c = container_of(to_delayed_work(work), struct umi_pps, work);
	struct umi_pps_sample s = {};
	u64 now;
	int ret = 0, online, next, control_ma;
	bool changed;

	mutex_lock(&c->lock);
	if (c->removing || c->suspended)
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
		}
		if (!umi_pps_get(c->sw, POWER_SUPPLY_PROP_ONLINE, &online) && !online)
			c->power = (struct umi_pps_power){0};
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
		if (!umi_pps_battery_ok(&s, UMI_CHARGE_MAX_UV / 1000) || s.soc < 15)
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
	umi_pps_power_update(&c->power, &s, c->thermal.virtual_mc, umi_pps_now());
	/* First lower measured current with voltage control, then tighten the
	 * adapter limit. Never ask for a limit already exceeded by the sample.
	 * A session never raises this current again after a thermal reduction.
	 */
	next = umi_pps_current(&c->caps, min(UMI_PPS_MAX_MA, s.thermal_bat_ma / 2));
	if (next < 0) {
		ret = -ERANGE;
		goto fail;
	}
	control_ma = min(c->request_ma, next);
	if (next < c->request_ma && s.ibus_ma <= next - 100) {
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
	next = umi_pps_power_next_mv(&s, &c->caps, c->max_bat_mv,
				     control_ma, c->request_mv, &c->power);
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
	dev_info(c->dev, "miseed-pps state=%u vbus_mv=%d ibus_ma=%d vbat_mv=%d pump_vbat_mv=%d ibat_ma=%d request_mv=%d request_ma=%d thermal_mc=%d thermal_level=%d thermal_bat_ma=%d target_mw=%d filtered_mw=%d steady=%d\n",
		 c->state, s.vbus_mv, s.ibus_ma, s.vbat_mv, s.pump_vbat_mv, s.ibat_ma,
		 c->request_mv, c->request_ma,
		 c->thermal.virtual_mc, c->thermal.level, s.thermal_bat_ma,
		 c->power.target_mw, c->power.filtered_mw, c->power.steady);
	goto out;
fail:
	if (c->state == PPS_WAIT_PLUG && !c->claimed && !c->pps) {
		if (ret != c->last_error)
			dev_info(c->dev, "miseed-pps pre-claim error=%d\n", ret);
		c->last_error = ret;
		goto out;
	}
	/* A physical detach can land between a PPS request and its fresh BQ
	 * sample. The pump then drops CHG_EN as VBUS disappears, which otherwise
	 * looks like a hardware fault while the previous ONLINE value is stale.
	 * Preserve the diagnostic, but finish the normal detach path when TCPM now
	 * confirms that the source is gone. I/O/timeout/protection errors must not
	 * be hidden by a concurrent detach, including partial latch reads. Stop
	 * still keeps SMB5 ownership if either stop mechanism fails.
	 */
	if (c->pps && (ret == -ENOTCONN ||
		      (ret == -ERANGE && umi_pps_pump_lost(c, &s)))) {
		int online;

		if (!umi_pps_get(c->pd, POWER_SUPPLY_PROP_ONLINE, &online) && !online) {
			dev_info(c->dev, "miseed-pps detached during control cycle error=%d vbus_mv=%d ibus_ma=%d vbat_mv=%d pump_vbat_mv=%d ibat_ma=%d fault=%d alarm=%d bus_low=%d bus_high=%d switching=%d bq_fault=0x%02x bq_latched=0x%02x bq_alarm=0x%02x bq_status=0x%02x bq_watchdog=0x%02x bq_control=0x%02x\n",
				 ret, s.vbus_mv, s.ibus_ma, s.vbat_mv, s.pump_vbat_mv,
				 s.ibat_ma, s.fault, s.alarm, s.bus_low, s.bus_high,
				 s.switching, s.fault_raw, s.latched_raw, s.alarm_raw,
				 s.status_raw, s.watchdog_raw, s.control_raw);
			umi_pps_stop(c, PPS_WAIT_PLUG, 0);
			goto out;
		}
	}
	dev_info(c->dev, "miseed-pps failure state=%u error=%d request_mv=%d request_ma=%d max_bat_mv=%d vbus_mv=%d ibus_ma=%d vbat_mv=%d pump_vbat_mv=%d ibat_ma=%d fault=%d alarm=%d bus_low=%d bus_high=%d switching=%d bq_fault=0x%02x bq_latched=0x%02x bq_alarm=0x%02x bq_status=0x%02x bq_watchdog=0x%02x bq_control=0x%02x\n",
		 c->state, ret, c->request_mv, c->request_ma, c->max_bat_mv,
		 s.vbus_mv, s.ibus_ma, s.vbat_mv, s.pump_vbat_mv,
		 s.ibat_ma, s.fault, s.alarm, s.bus_low, s.bus_high, s.switching,
		 s.fault_raw, s.latched_raw, s.alarm_raw, s.status_raw,
		 s.watchdog_raw, s.control_raw);
	umi_pps_stop(c, PPS_FAULT, ret);
out:
	/* Keep observing unplug after manual stop without active requests. */
	if (!c->removing && !c->suspended)
		mod_delayed_work(system_wq, &c->work, msecs_to_jiffies(UMI_PPS_PERIOD_MS));
	changed = umi_pps_changed(c);
	mutex_unlock(&c->lock);
	if (changed)
		power_supply_changed(c->psy);
}
