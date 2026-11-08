/* SPDX-License-Identifier: GPL-2.0-only */
/* Umi PPS policy adapted from TI/LineageOS pd_policy_manager.c/.h at
 * 71b13e62f057a649b77fe4062feb73ee72ad609c. See NOTICE.md.
 * Units here are mV, mA and deci-C. No floating point or hardware accesses.
 */
#ifndef MISEED_UMI_PPS_POLICY_H
#define MISEED_UMI_PPS_POLICY_H

#include "umi_charge_policy.h"

#define UMI_PPS_STEP_MV 20
#define UMI_PPS_STEP_MA 50
#define UMI_PPS_MAX_MV 9500
#define UMI_PPS_MAX_MA 3000
#define UMI_PPS_BAT_MAX_MA 6000
#define UMI_PPS_MIN_BAT_MV 3500
#define UMI_PPS_HEADROOM_MV 400
#define UMI_PPS_TAPER_MA 2200
#define UMI_PPS_TAPER_MS 10000
#define UMI_PPS_PERIOD_MS 200
#define UMI_PPS_START_MS 5000

struct umi_pps_caps {
	int min_mv, max_mv, max_ma;
};

struct umi_pps_sample {
	int online, present, good, temp, soc;
	int vbat_mv, ibat_ma, vbus_mv, ibus_ma;
	int pump_vbat_mv, pump_ibat_ma;
	int fault, alarm, bus_low, bus_high, switching;
};

static inline int umi_pps_min(int a, int b) { return a < b ? a : b; }
static inline int umi_pps_max(int a, int b) { return a > b ? a : b; }

/* Never silently round a requested threshold to a less protective value. */
static inline int umi_pps_encode(int value, int base, int step, int mask)
{
	if (value < base || value > base + step * mask || (value - base) % step)
		return -1;
	return (value - base) / step;
}

static inline int umi_pps_battery_ok(const struct umi_pps_sample *s, int max_mv)
{
	return s->online && s->present && s->good &&
		s->temp > UMI_CHARGE_COOL_DECIC && s->temp <= UMI_CHARGE_WARM_DECIC &&
		s->soc >= 0 && s->soc < UMI_CHARGE_UPPER_SOC &&
		s->vbat_mv >= UMI_PPS_MIN_BAT_MV && s->vbat_mv < max_mv &&
		max_mv > UMI_PPS_MIN_BAT_MV && max_mv <= UMI_CHARGE_MAX_UV / 1000 &&
		s->ibat_ma >= -UMI_PPS_BAT_MAX_MA && s->ibat_ma <= UMI_PPS_BAT_MAX_MA;
}

static inline int umi_pps_caps_ok(const struct umi_pps_caps *c)
{
	/* Bound arithmetic and refuse an APDO unsuited to this 2:1 topology. */
	return c->min_mv > 0 && c->min_mv <= UMI_PPS_MAX_MV &&
		c->max_mv >= 10000 && c->max_mv <= 21000 &&
		c->max_ma >= 2000 && c->max_ma <= 5000;
}

static inline int umi_pps_target(const struct umi_pps_caps *c, int vbat_mv)
{
	int mv;

	if (!umi_pps_caps_ok(c) || vbat_mv < UMI_PPS_MIN_BAT_MV ||
	    vbat_mv >= UMI_CHARGE_MAX_UV / 1000)
		return -1;
	mv = umi_pps_max(2 * vbat_mv + UMI_PPS_HEADROOM_MV, c->min_mv);
	mv = (mv + UMI_PPS_STEP_MV - 1) / UMI_PPS_STEP_MV * UMI_PPS_STEP_MV;
	return mv <= umi_pps_min(c->max_mv, UMI_PPS_MAX_MV) ? mv : -1;
}

static inline int umi_pps_current(const struct umi_pps_caps *c, int board_ma)
{
	int ma;

	if (!umi_pps_caps_ok(c) || board_ma < 2000 || board_ma > UMI_PPS_MAX_MA)
		return -1;
	ma = umi_pps_min(c->max_ma, board_ma);
	return ma / UMI_PPS_STEP_MA * UMI_PPS_STEP_MA;
}

/* Reject inconsistent ADCs, reverse current and any latched protection event. */
static inline int umi_pps_sample_ok(const struct umi_pps_sample *s,
		int max_mv, int max_ma, int requested_mv)
{
	int delta = s->pump_vbat_mv - s->vbat_mv;

	return umi_pps_battery_ok(s, max_mv) && !s->fault &&
		s->pump_vbat_mv >= UMI_PPS_MIN_BAT_MV && s->pump_vbat_mv < max_mv &&
		delta >= -50 && delta <= 50 &&
		s->pump_ibat_ma >= 0 && s->pump_ibat_ma <= UMI_PPS_BAT_MAX_MA &&
		s->ibus_ma >= 0 && s->ibus_ma <= max_ma &&
		s->vbus_mv >= 2 * UMI_PPS_MIN_BAT_MV && s->vbus_mv <= UMI_PPS_MAX_MV &&
		s->vbus_mv >= requested_mv - 200 && s->vbus_mv <= requested_mv + 200;
}

/* Voltage / battery-current / bus-current loops: the most restrictive wins. */
static inline int umi_pps_next_mv(const struct umi_pps_sample *s,
		const struct umi_pps_caps *c, int max_bat_mv, int max_ma, int mv)
{
	int step = 0, next;

	if (!umi_pps_caps_ok(c))
		return -1;
	if (s->bus_high || s->alarm || s->vbat_mv >= max_bat_mv - 20 ||
	    s->pump_ibat_ma >= UMI_PPS_BAT_MAX_MA - 100 ||
	    s->ibat_ma >= UMI_PPS_BAT_MAX_MA - 100 || s->ibus_ma >= max_ma - 100)
		step = -UMI_PPS_STEP_MV;
	else if (s->bus_low || (s->vbat_mv < max_bat_mv - 30 &&
		 s->ibat_ma < UMI_PPS_BAT_MAX_MA - 200 && s->ibus_ma < max_ma - 200))
		step = UMI_PPS_STEP_MV;
	next = mv + step;
	if (next < c->min_mv || next > umi_pps_min(c->max_mv, UMI_PPS_MAX_MV))
		return -1;
	return next;
}
#endif
