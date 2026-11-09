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
#define UMI_PPS_OPERATION_MS 10000
#define UMI_PPS_MAX_SOC 95 /* LineageOS CAPACITY_TOO_HIGH_THR for Umi. */

/* Normal-profile facts observed on this Umi. The opaque Android daemon is
 * not redistributed. Its generic_set_level selects target / 100 while the
 * screen-state input is nonzero, target % 100 otherwise (targets >= 100).
 * Unknown screen state uses the stricter result, never an unrestricted vote.
 */
#define UMI_PPS_THERMAL_SENSORS 6
#define UMI_PPS_THERMAL_STEPS 9
struct umi_pps_thermal {
	int step, level, virtual_mc, max_bat_ma;
};

static inline int umi_pps_thermal_level(int target, int screen_state)
{
	int on, off;

	if (target < 0)
		return -1;
	on = target < 100 ? target : target / 100;
	off = target < 100 ? target : target % 100;
	if (on > 15 || off > 15)
		return -1;
	if (screen_state < 0)
		return on > off ? on : off;
	return screen_state ? on : off;
}

static inline int umi_pps_thermal_update(struct umi_pps_thermal *t,
		const int temp_mc[UMI_PPS_THERMAL_SENSORS], int screen_state,
		int board_comp_mc)
{
	/* battery, cam_therm1, conn_therm, quiet_therm, wireless_therm, xo_therm */
	static const int weight[] = {223, 150, 440, -143, 115, 149};
	static const int trigger[] = {41000, 43000, 45000, 46000, 47000,
		48000, 49000, 50000, 63000};
	static const int clear[] = {38000, 41000, 43000, 45000, 46000,
		47000, 48000, 49000, 51000};
	static const int target[] = {0, 803, 1106, 1206, 1206, 1209,
		1410, 1512, 1514, 1515};
	/* Fixed LOS Umi thermal-fcc-pps-bq table, battery-side mA. */
	static const int fcc_ma[] = {6000, 5700, 5475, 5250, 5025, 4800,
		4575, 4350, 4100, 3500, 3000, 2400, 2000, 1500, 1000, 500};
	long long sum = 0, virtual_mc;
	int i, step = t->step, level;

	if (step < 0 || step > UMI_PPS_THERMAL_STEPS || board_comp_mc != 0)
		return -1; /* Only the observed no-board-compensation profile is qualified. */
	for (i = 0; i < UMI_PPS_THERMAL_SENSORS; i++) {
		/* ADC thermistor range; reject endpoints as possible open/short rails. */
		if (temp_mc[i] <= -40000 || temp_mc[i] >= 125000)
			return -1;
		sum += (long long)temp_mc[i] * weight[i];
	}
	virtual_mc = sum / 1000 + 4541;
	while (step < UMI_PPS_THERMAL_STEPS && virtual_mc >= trigger[step])
		step++;
	while (step > 0 && virtual_mc < clear[step - 1])
		step--;
	level = umi_pps_thermal_level(target[step], screen_state);
	if (level < 0)
		return -1;
	t->step = step;
	t->level = level;
	t->virtual_mc = (int)virtual_mc;
	t->max_bat_ma = fcc_ma[level];
	return 0;
}

struct umi_pps_caps {
	int min_mv, max_mv, max_ma;
};

struct umi_pps_sample {
	int online, present, good, temp, soc;
	int vbat_mv, ibat_ma, vbus_mv, ibus_ma;
	int pump_vbat_mv;
	int thermal_bat_ma;
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
		s->soc >= 0 && s->soc < UMI_PPS_MAX_SOC &&
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

/* Reject out-of-range voltage ADCs, reverse bus current and latched faults.
 * LOS takes battery current from BMS: a negative NET battery current can mean
 * system load exceeds pump output, and is not evidence of reverse pump flow.
 */
static inline int umi_pps_sample_limits(const struct umi_pps_sample *s,
		int max_mv, int max_ma)
{
	return umi_pps_battery_ok(s, max_mv) && !s->fault &&
		s->thermal_bat_ma > 0 && s->thermal_bat_ma <= UMI_PPS_BAT_MAX_MA &&
		s->ibat_ma <= s->thermal_bat_ma &&
		s->pump_vbat_mv >= UMI_PPS_MIN_BAT_MV && s->pump_vbat_mv < max_mv &&
		s->ibus_ma >= 0 && s->ibus_ma <= max_ma &&
		s->vbus_mv >= 2 * UMI_PPS_MIN_BAT_MV && s->vbus_mv <= UMI_PPS_MAX_MV;
}

static inline int umi_pps_sample_ok(const struct umi_pps_sample *s,
		int max_mv, int max_ma, int requested_mv, int starting)
{
	int delta = s->pump_vbat_mv - s->vbat_mv;

	/* Keep the extra agreement gate before enabling the pump. LOS Umi uses
	 * the pump VBAT for its running loop, not a cross-chip equality test.
	 * Both independent absolute battery limits still apply while running.
	 */
	/* The requested voltage is the source contract, not the loaded pump
	 * input. LOS RUN regulates from measured battery/bus current and pump
	 * status rather than requiring a fixed source-to-pump voltage drop.
	 * Keep the lower agreement gate before enable, the upper gate always,
	 * and absolute limits plus RUN switching/bus status checks in the worker.
	 */
	return umi_pps_sample_limits(s, max_mv, max_ma) &&
		(!starting || (delta >= -50 && delta <= 50 &&
			       s->vbus_mv >= requested_mv - 200)) &&
		s->vbus_mv <= requested_mv + 200;
}

static inline int umi_pps_control_vbat(const struct umi_pps_sample *s)
{
	/* Use LOS's pump measurement, retaining a higher BMS value as an
	 * additional conservative bound for voltage regulation and taper.
	 */
	return umi_pps_max(s->vbat_mv, s->pump_vbat_mv);
}

/* Voltage / battery-current / bus-current loops: the most restrictive wins. */
static inline int umi_pps_next_mv(const struct umi_pps_sample *s,
		const struct umi_pps_caps *c, int max_bat_mv, int max_ma, int mv)
{
	int step = 0, next, vbat_mv = umi_pps_control_vbat(s);

	if (!umi_pps_caps_ok(c))
		return -1;
	if (s->bus_high || s->alarm || vbat_mv >= max_bat_mv - 20 ||
	    s->ibat_ma >= s->thermal_bat_ma - 100 || s->ibus_ma >= max_ma - 100)
		step = -UMI_PPS_STEP_MV;
	else if (s->bus_low || (vbat_mv < max_bat_mv - 30 &&
		 s->ibat_ma < s->thermal_bat_ma - 200 && s->ibus_ma < max_ma - 200))
		step = UMI_PPS_STEP_MV;
	next = mv + step;
	if (next < c->min_mv || next > umi_pps_min(c->max_mv, UMI_PPS_MAX_MV))
		return -1;
	return next;
}
#endif
