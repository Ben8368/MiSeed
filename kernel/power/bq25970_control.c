// SPDX-License-Identifier: GPL-2.0-only
/* Serialized chip operations. Register provenance is in bq25970.c/NOTICE.md. */
#include <linux/module.h>
#include <linux/regmap.h>
#include "bq25970_internal.h"
#include "bq25970_board.h"

int bq25970_set_board_image(struct bq25970_device *bq,
		const struct bq25970_reg *image, unsigned int count)
{
	unsigned int i;
	int ret = 0;

	if (!image || !count || count > BQ25970_MAX_PROTECTION)
		return -EINVAL;
	/* Only the migration adapter can supply an image; it cannot start a pump. */
	for (i = 0; i < count; i++)
		if ((image[i].reg & ~BQ_IMAGE_VERIFY_ONLY) > 0x2e || image[i].mask > 0xff ||
		    (image[i].value & ~image[i].mask) ||
		    (image[i].reg == BQ_CONTROL && (image[i].value & BQ_ENABLE)))
			return -EINVAL;
	mutex_lock(&bq->lock);
	if (bq->nprotection || bq->configured || bq->owner)
		ret = -EBUSY;
	else {
		memcpy(bq->protection, image, count * sizeof(*image));
		bq->nprotection = count;
	}
	mutex_unlock(&bq->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_set_board_image);

int bq25970_stop(struct bq25970_device *bq)
{
	bool changed;
	int ret;

	mutex_lock(&bq->lock);
	ret = bq25970_disable(bq->map);
	changed = bq->sample_valid || bq->sample_error != ret ||
		(!ret && (bq->cached.enabled || bq->cached.switching));
	bq->sample_valid = false;
	bq->sample_error = ret;
	if (!ret) {
		bq->cached.enabled = false;
		bq->cached.switching = false;
	}
	mutex_unlock(&bq->lock);
	if (changed && bq->psy)
		power_supply_changed(bq->psy);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_stop);

int bq25970_start(struct bq25970_device *bq)
{
	int ret;

	mutex_lock(&bq->lock);
	if (!bq->owner || !bq->configured)
		ret = -EPERM;
	else
		ret = bq25970_update(bq->map, BQ_CONTROL, BQ_ENABLE, BQ_ENABLE);
	if (!ret) {
		bq->cached.enabled = true;
		bq->sample_valid = false;
	}
	mutex_unlock(&bq->lock);
	if (!ret && bq->psy)
		power_supply_changed(bq->psy);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_start);

int bq25970_prepare(struct bq25970_device *bq)
{
	int ret;

	mutex_lock(&bq->lock);
	if (!bq->owner || !bq->nprotection) {
		ret = -EPERM;
		goto out;
	}
	bq->configured = false;
	ret = bq25970_configure(bq->map, bq->protection, bq->nprotection);
	if (ret == -ETIME) {
		bq->cached.watchdog |= BQ_WD_TIMEOUT_FLAG;
		bq->cached.hardware_fault = true;
	}
	if (ret)
		bq->sample_error = ret;
	if (!ret) {
		/* A new stopped session explicitly acknowledges the previous cache. */
		memset(&bq->cached, 0, sizeof(bq->cached));
		bq->sample_valid = false;
		bq->sample_error = 0;
		bq->configured = true;
	}
out:
	mutex_unlock(&bq->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_prepare);

int bq25970_is_switching(struct bq25970_device *bq, bool *switching)
{
	unsigned int status;
	int ret;

	mutex_lock(&bq->lock);
	ret = bq25970_live_status(bq->map, &status);
	if (!ret)
		*switching = !!(status & BIT(2));
	mutex_unlock(&bq->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_is_switching);

int bq25970_check_configuration(struct bq25970_device *bq)
{
	int ret;

	mutex_lock(&bq->lock);
	ret = bq->configured ? bq25970_verify(bq->map, bq->protection,
					    bq->nprotection) : -ENODATA;
	if (ret == -ETIME) {
		bq->cached.watchdog |= BQ_WD_TIMEOUT_FLAG;
		bq->cached.hardware_fault = true;
	}
	if (ret) {
		bq->sample_error = ret;
		bq->sample_valid = false;
	}
	mutex_unlock(&bq->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_check_configuration);
