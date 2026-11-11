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
#ifndef MISEED_UMI_PPS_INTERNAL_H
#define MISEED_UMI_PPS_INTERNAL_H

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
#include <linux/thermal.h>

#include "umi_pps_policy.h"
#include "umi_smb5.h"
#include "bq25970.h"

#define PPS_FIXED 1
#define PPS_PROGRAMMABLE 2

enum umi_pps_state { PPS_OFF, PPS_WAIT_PLUG,
	PPS_SETTLE, PPS_RUN, PPS_DONE, PPS_FAULT };

/* Discrete wired supply state last queued for a power_supply notification. */
struct umi_pps_report {
	enum umi_pps_state state;
	int online, usb_type, pump_status, health;
	bool manual_stop, configured, stop_pending;
};

/* Expected writable protection image, checked during every control cycle. */
struct umi_pps {
	struct device *dev;
	struct regmap *map;
	struct bq25970_device *chip;
	struct power_supply *pd, *bms, *sw, *psy;
	struct thermal_zone_device *tzd;
	struct thermal_cooling_device *cooling;
	unsigned long cooling_state;
	bool ready;
	struct delayed_work work;
	struct mutex lock;
	struct umi_pps_caps caps;
	struct iio_channel *thermal_channels[UMI_PPS_THERMAL_SENSORS - 1];
	struct umi_pps_thermal thermal;
	struct umi_pps_power power;
	int thermal_comp_mc;
	struct bq25970_reg protection[20];
	unsigned int nprotection;
	enum umi_pps_state state;
	bool claimed, configured, pps, removing, stop_pending, manual_stop;
	bool suspended, resume_pps;
	u64 deadline, start_deadline, taper_start, last_pps_request_ms;
	int request_mv, request_ma, max_bat_mv;
	int last_error;
	struct umi_pps_report reported;
	bool reported_valid;
};


u64 umi_pps_now(void);
int umi_pps_get(struct power_supply *psy, enum power_supply_property p, int *v);
int umi_pps_update(struct umi_pps *c, unsigned int reg,
		unsigned int mask, unsigned int value);
int umi_pps_disable(struct umi_pps *c);
int umi_pps_stop(struct umi_pps *c, enum umi_pps_state state, int reason);
void umi_pps_work(struct work_struct *work);
bool umi_pps_changed(struct umi_pps *c);
int umi_pps_parse(struct umi_pps *c);
int umi_pps_verify(struct umi_pps *c);
int umi_pps_prepare(struct umi_pps *c);
int umi_pps_battery(struct umi_pps *c, struct umi_pps_sample *s);
int umi_pps_adc(struct umi_pps *c, int channel, int *value);
int umi_pps_sample(struct umi_pps *c, struct umi_pps_sample *s);
extern const struct power_supply_desc umi_pps_desc;
extern const struct thermal_zone_device_ops umi_pps_thermal_ops;
extern const struct thermal_cooling_device_ops umi_pps_cooling_ops;

#endif
