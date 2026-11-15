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
int umi_pps_check_deadline(struct umi_pps *c)
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

int umi_pps_set(struct power_supply *psy, enum power_supply_property p, int v)
{
	union power_supply_propval val = { .intval = v };

	return power_supply_set_property(psy, p, &val);
}

/* TCPM may be servicing a source AMS or discard a colliding request. Retry
 * only that transient result, within the existing operation/startup windows.
 * Transport errors/timeouts still go straight to the stop/recovery path.
 */
int umi_pps_negotiate(struct umi_pps *c, enum power_supply_property p, int v)
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

int umi_pps_caps(struct umi_pps *c)
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

int umi_pps_contract(struct umi_pps *c)
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

int umi_pps_request(struct umi_pps *c, int mv)
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
