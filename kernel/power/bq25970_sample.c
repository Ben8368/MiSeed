// SPDX-License-Identifier: GPL-2.0-only
/* Single serialized owner of destructive fault reads and ADC conversion. */
#include <linux/module.h>
#include <linux/regmap.h>
#include "bq25970_internal.h"

int bq25970_read_sample(struct bq25970_device *bq, struct bq25970_sample *sample)
{
	struct bq25970_sample previous;
	struct bq25970_status status;
	int ret, raw;
	bool changed;

	mutex_lock(&bq->lock);
	previous = bq->cached;
	*sample = previous;
	status = (struct bq25970_status) {
		.fault = sample->fault, .latched = sample->latched,
		.alarm = sample->alarm, .status = sample->status,
		.watchdog = sample->watchdog, .control = sample->control,
	};
	ret = bq->configured ? bq25970_verify(bq->map, bq->protection,
					    bq->nprotection) : 0;
	if (ret == -ETIME)
		status.watchdog |= BQ_WD_TIMEOUT_FLAG;
	if (!ret)
		ret = bq25970_fault_snapshot(bq->map, &status);
	/* Preserve both consumed latches and partial reads on subsequent failure. */
	sample->fault = previous.fault | status.fault;
	sample->latched = previous.latched | status.latched;
	sample->alarm = status.alarm;
	sample->status = status.status;
	sample->watchdog = status.watchdog | (previous.watchdog & BQ_WD_TIMEOUT_FLAG);
	sample->control = status.control;
	sample->enabled = !!(status.control & BQ_ENABLE);
	sample->switching = !!(status.status & BIT(2));
	sample->hardware_fault = sample->fault || sample->latched ||
		(status.status & 0xcb) || (sample->watchdog & BQ_WD_TIMEOUT_FLAG);
	sample->input_present = !!(status.alarm & BIT(1));
	sample->alarm_active = !!(status.alarm & 0xf0);
	sample->bus_low = !!(status.status & BIT(5));
	sample->bus_high = !!(status.status & BIT(4));
	sample->clean_disabled = !sample->hardware_fault && !status.status &&
		status.alarm == BIT(1) && !sample->enabled;
	if (!ret && bq->configured) {
		ret = bq25970_adc(bq->map, 0, &raw);
		if (!ret)
			sample->ibus_ua = raw * 1000;
		if (!ret)
			ret = bq25970_adc(bq->map, 1, &raw);
		if (!ret)
			sample->vbus_uv = raw * 1000;
		if (!ret)
			ret = bq25970_adc(bq->map, 4, &raw);
		if (!ret)
			sample->vbat_uv = raw * 1000;
	}
	changed = !bq->sample_valid || bq->sample_error != ret ||
		previous.hardware_fault != sample->hardware_fault ||
		previous.switching != sample->switching;
	bq->cached = *sample;
	bq->sample_error = ret;
	bq->sample_valid = !ret;
	mutex_unlock(&bq->lock);
	if (changed && bq->psy)
		power_supply_changed(bq->psy);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_read_sample);
