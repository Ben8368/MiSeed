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

/* thermal uses milli-C; the FG power_supply uses deci-C. This callback reads
 * sensors without changing the protection controller's hysteresis state.
 */
static int umi_pps_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct umi_pps *c = thermal_zone_device_priv(tz);
	struct umi_pps_thermal thermal = {};
	int samples[UMI_PPS_THERMAL_SENSORS], ret, i;

	mutex_lock(&c->lock);
	if (c->removing || c->suspended || !c->ready) {
		ret = -EAGAIN;
		goto out;
	}
	ret = umi_pps_get(c->bms, POWER_SUPPLY_PROP_TEMP, &samples[0]);
	if (ret)
		goto out;
	if (samples[0] <= -400 || samples[0] >= 1250) {
		ret = -ERANGE;
		goto out;
	}
	samples[0] *= 100;
	for (i = 1; i < UMI_PPS_THERMAL_SENSORS; i++) {
		ret = iio_read_channel_processed(c->thermal_channels[i - 1], &samples[i]);
		if (ret < 0)
			goto out;
	}
	ret = umi_pps_thermal_update(&thermal, samples, -1, c->thermal_comp_mc);
	if (ret)
		ret = -ERANGE;
	else
		*temp = thermal.virtual_mc;
out:
	mutex_unlock(&c->lock);
	return ret;
}

const struct thermal_zone_device_ops umi_pps_thermal_ops = {
	.get_temp = umi_pps_get_temp,
};

static int umi_pps_get_max_state(struct thermal_cooling_device *dev, unsigned long *state)
{
	*state = 15;
	return 0;
}

static int umi_pps_get_cur_state(struct thermal_cooling_device *dev, unsigned long *state)
{
	struct umi_pps *c = dev->devdata;

	mutex_lock(&c->lock);
	*state = c->cooling_state;
	mutex_unlock(&c->lock);
	return 0;
}

static int umi_pps_set_cur_state(struct thermal_cooling_device *dev, unsigned long state)
{
	struct umi_pps *c = dev->devdata;
	int ret = 0;

	if (umi_pps_cooling_current(state) < 0)
		return -EINVAL;
	mutex_lock(&c->lock);
	if (c->removing || !c->ready) {
		ret = -EAGAIN;
	} else if (c->cooling_state != state) {
		c->cooling_state = state;
		mod_delayed_work(system_wq, &c->work, 0);
	}
	mutex_unlock(&c->lock);
	return ret;
}

const struct thermal_cooling_device_ops umi_pps_cooling_ops = {
	.get_max_state = umi_pps_get_max_state,
	.get_cur_state = umi_pps_get_cur_state,
	.set_cur_state = umi_pps_set_cur_state,
};
