// SPDX-License-Identifier: GPL-2.0-only
/*
 * BQ25970 register access, separated from the Umi wired PPS policy.
 * Register encodings adapted from LineageOS 71b13e62f057a649b77fe4062feb73ee72ad609c.
 * BQ source: Copyright (C) 2017 Texas Instruments; GPL version 2.
 * See NOTICE.md. No private UAPI or exported cross-driver symbols.
 */
#include <linux/errno.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "bq25970.h"

int bq25970_update(struct regmap *map, unsigned int reg,
		   unsigned int mask, unsigned int value)
{
	unsigned int readback;
	int ret = regmap_update_bits(map, reg, mask, value);

	if (!ret)
		ret = regmap_read(map, reg, &readback);
	if (!ret && (readback & mask) != value)
		ret = -EIO;
	return ret;
}

int bq25970_disable(struct regmap *map)
{
	unsigned int status;
	int ret = bq25970_update(map, BQ_CONTROL, BQ_ENABLE, 0);

	if (!ret)
		ret = regmap_read(map, 0x0a, &status);
	if (!ret && (status & BIT(2)))
		ret = -EBUSY;
	return ret;
}

int bq25970_verify(struct regmap *map,
		   const struct bq25970_reg *protection, unsigned int count)
{
	unsigned int i, v;
	int ret;

	for (i = 0; i < count; i++) {
		const struct bq25970_reg *r = &protection[i];

		ret = regmap_read(map, r->reg, &v);
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

int bq25970_adc(struct regmap *map, int channel, int *value)
{
	u8 bytes[2];
	int ret = regmap_bulk_read(map, BQ_ADC_BASE + 2 * channel, bytes, 2);

	if (!ret)
		*value = (s16)((bytes[0] << 8) | bytes[1]);
	return ret;
}
