// SPDX-License-Identifier: GPL-2.0-only
/* Umi-only migration image.
 * Register encoding: LineageOS 71b13e62f057a649b77fe4062feb73ee72ad609c.
 * Copyright (C) 2017 Texas Instruments. Original policy: Fei Jiang
 * <jiangfei1@xiaomi.com>; a0220433 (Mar 27, 2017).
 * TI/LOS attribution retained in NOTICE.md and
 * bq25970.c. These observed settings are NOT generic BQ25970 defaults.
 * In particular, 0x2b polarity and BQ25968-derived watchdog timing remain
 * unqualified. No runtime policy receives this image or a register handle.
 */
#include <linux/device.h>
#include <linux/property.h>
#include "bq25970_board.h"
#include "bq25970_device.h"
#include "umi_bq25970.h"
struct umi_bq_profile {
	struct device *dev;
	struct bq25970_reg protection[20];
	unsigned int nprotection;
};
static int umi_bq_encode(int value, int base, int step, int mask)
{
	if (value < base || (value - base) % step || (value - base) / step > mask)
		return -ERANGE;
	return (value - base) / step;
}
static int umi_bq_property(struct umi_bq_profile *c, const char *name, int *value)
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

static int umi_bq_add_limit(struct umi_bq_profile *c, const char *property,
		int reg, int mask, int base, int step, int min, int max)
{
	int value, encoded, ret = umi_bq_property(c, property, &value);

	if (ret)
		return ret;
	encoded = umi_bq_encode(value, base, step, mask & 0x7f);
	if (value < min || value > max || encoded < 0)
		return dev_err_probe(c->dev, -ERANGE, "invalid exact threshold %s\n", property);
	c->protection[c->nprotection++] = (struct bq25970_reg){reg, mask, encoded};
	return 0;
}

static int umi_bq_parse(struct umi_bq_profile *c)
{
	int ret;

	/* Match active LOS Umi configuration: battery OCP and the three BQ TS
	 * comparators are disabled. Thermal/FCC control is supplied by the same
	 * external LOS Umi temperature policy, ported above to mainline APIs.
	 * Do not make parameters mandatory when the reference tree omits them.
	 */
#define LIMIT(name, reg, mask, base, step, min, max) do { \
	ret = umi_bq_add_limit(c, name, reg, mask, base, step, min, max); \
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
	c->protection[c->nprotection++] = (struct bq25970_reg){0x02, 0x80, 0x80};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x03, 0x80, 0x80};
	/* LOS DTS also disables the battery UCP alarm. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x04, 0x80, 0x80};
	/* Standalone mode, charge off, BQ therm comparators disabled per LOS. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x0c, 0x67, 0x07};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x28, 0xff, 0x15};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x29, 0xff, 0x15};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x2a, 0xff, (1450 - 300) / 5};
	/* Keep the exact register configuration observed on this LOS device. */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x2b, 0xf7, 0xd0};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x2c, 0xf0, 0};
	/* LOS init_adc: continuous/averaging/13 bits; IBUS, VBUS, VAC, VBAT.
	 * IBAT is supplied by BMS, not the unqualified pump sense-resistor ADC.
	 */
	c->protection[c->nprotection++] = (struct bq25970_reg){0x14, 0xed, 0xa8};
	c->protection[c->nprotection++] = (struct bq25970_reg){0x15, 0xff, 0x2f};
	/* TI TIDA-01582 disables the watchdog by setting 0x0b[2]. This
	 * policy instead uses the sibling BQ25968-documented 30 s interval;
	 * successful control-loop I2C transactions provide the refresh.
	 */
	c->protection[c->nprotection++] = (struct bq25970_reg){BQ_WATCHDOG,
		BQ_WD_CONFIG_MASK, BQ_WD_ENABLED_30S};
	/* Preserve the observed VOUT bit without guessing or writing its polarity. */
	c->protection[c->nprotection++] = (struct bq25970_reg){
		0x2b | BQ_IMAGE_VERIFY_ONLY, BIT(3), 0};
	return 0;
}

int umi_bq25970_profile(struct device *dev, struct bq25970_device *bq)
{
	struct umi_bq_profile profile = { .dev = dev };
	int ret = umi_bq_parse(&profile);

	return ret ? ret : bq25970_set_board_image(bq, profile.protection,
						profile.nprotection);
}
