// SPDX-License-Identifier: AGPL-3.0-only
/* Synthetic register fixtures, not board calibration or physical writes. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef int16_t s16;
#define BIT(n) (1U << (n))
struct regmap { int unused; };
static u8 regs[256];
static int io_count, fail_at, latch_reads, writes;
static int regmap_read(struct regmap *map, unsigned int reg, unsigned int *value)
{
	if (++io_count == fail_at)
		return -EIO;
	*value = regs[reg];
	if (reg == 0x11) {
		latch_reads++;
		regs[reg] = 0;
	}
	return 0;
}
static int regmap_update_bits(struct regmap *map, unsigned int reg,
			      unsigned int mask, unsigned int value)
{
	if (++io_count == fail_at)
		return -EIO;
	writes++;
	regs[reg] = (regs[reg] & ~mask) | value;
	return 0;
}
static int regmap_bulk_read(struct regmap *map, unsigned int reg, void *value, unsigned int count)
{
	if (++io_count == fail_at)
		return -EIO;
	memcpy(value, regs + reg, count);
	return 0;
}
/* DRIVER_UNDER_TEST */

static void reset(void)
{
	memset(regs, 0, sizeof(regs));
	io_count = fail_at = latch_reads = writes = 0;
	regs[BQ_ID] = 0x10;
}

int main(void)
{
	struct bq25970_status snapshot;
	unsigned int status;
	int value;
	reset(); assert(!bq25970_check_identity(NULL));
	regs[BQ_ID] = 0x12; assert(bq25970_check_identity(NULL) == -ENODEV);
	regs[BQ_ID] = 0x10; regs[BQ_CONTROL] = 0x20;
	assert(bq25970_check_identity(NULL) == -EINVAL);
	for (int i = 1; i <= 2; i++) {
		reset(); fail_at = i;
		assert(bq25970_check_identity(NULL) == -EIO && io_count == i);
	}
	reset(); regs[0x11] = 0x40; regs[0x0a] = 4;
	assert(!bq25970_live_status(NULL, &status) && status == 4);
	assert(!latch_reads && regs[0x11] == 0x40);
	assert(!bq25970_fault_snapshot(NULL, &snapshot));
	assert(snapshot.latched == 0x40 && snapshot.status == 4 && latch_reads == 1);
	assert(regs[0x11] == 0);
	for (int i = 1; i <= 6; i++) {
		reset(); fail_at = i;
		assert(bq25970_fault_snapshot(NULL, &snapshot) == -EIO && io_count == i);
	}
	reset(); value = 123;
	assert(bq25970_adc(NULL, -1, &value) == -EINVAL);
	assert(bq25970_adc(NULL, 9, &value) == -EINVAL && !io_count && value == 123);
	regs[BQ_ADC_BASE] = 0xff; regs[BQ_ADC_BASE + 1] = 0xfe;
	assert(!bq25970_adc(NULL, 0, &value) && value == -2);
	/* Configuration must stop before applying the caller's protection image. */
	const struct bq25970_reg protection[] = {{0x00, 0xff, 0x2b}, {0x06, 0x7f, 0x79}};
	reset(); fail_at = 1;
	assert(bq25970_configure(NULL, protection, 2) == -EIO && !writes);
	reset(); regs[0x0a] = 4;
	assert(bq25970_configure(NULL, protection, 2) == -EBUSY && regs[0] == 0);
	reset(); assert(!bq25970_configure(NULL, protection, 2));
	assert(regs[0] == 0x2b && regs[6] == 0x79 && !(regs[BQ_CONTROL] & BQ_ENABLE));
	return 0;
}
