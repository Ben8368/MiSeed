// SPDX-License-Identifier: GPL-2.0-only
/* BQ25970 register/ADC backend, exported only to kernel consumers.
 * This layer knows no Umi policy, TCPM, fuel gauge or charger ownership.
 * Register provenance and attribution are retained in bq25970.c/NOTICE.md.
 */
#ifndef MISEED_BQ25970_H
#define MISEED_BQ25970_H

struct regmap;
struct bq25970_reg { unsigned int reg, mask, value; };
struct bq25970_status {
	unsigned int fault, latched, alarm, status, watchdog, control;
};

/* BQ25970 only. SC8551/NU2105/BQ25968 require distinct ADC/protection ports. */
#define BQ_ID 0x13
#define BQ_CONTROL 0x0c
#define BQ_ENABLE BIT(7)
#define BQ_WATCHDOG 0x0b
#define BQ_WD_DISABLED BIT(2)
#define BQ_ADC 0x14
#define BQ_ADC_BASE 0x16

int bq25970_update(struct regmap *map, unsigned int reg,
		   unsigned int mask, unsigned int value);
int bq25970_disable(struct regmap *map);
int bq25970_check_identity(struct regmap *map);
int bq25970_configure(struct regmap *map,
		     const struct bq25970_reg *protection, unsigned int count);
/* Only the serialized protection worker may consume the latched faults. */
int bq25970_fault_snapshot(struct regmap *map, struct bq25970_status *status);
/* Live status is safe for power_supply reads and notifications. */
int bq25970_live_status(struct regmap *map, unsigned int *status);
int bq25970_verify(struct regmap *map,
		   const struct bq25970_reg *protection, unsigned int count);
int bq25970_adc(struct regmap *map, int channel, int *value);

#endif
