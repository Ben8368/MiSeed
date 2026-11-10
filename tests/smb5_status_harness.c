// SPDX-License-Identifier: AGPL-3.0-only
/* Wired SMB5 register replay, including the observed 0x03/0x28 mismatch.
 * No write API is provided: status reads must not change charging controls.
 */
#include <assert.h>
#include <errno.h>
#include <string.h>
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((1U << ((h) + 1)) - 1) & ~((1U << (l)) - 1))
#define dev_err(...) ((void)0)
#include "qcom_pm8150b_regs.h"
enum { POWER_SUPPLY_STATUS_UNKNOWN, POWER_SUPPLY_STATUS_CHARGING,
       POWER_SUPPLY_STATUS_DISCHARGING, POWER_SUPPLY_STATUS_NOT_CHARGING,
       POWER_SUPPLY_STATUS_FULL };
struct smb5_chip { void *regmap; unsigned int base; };
static unsigned int usb = 0x10;
static unsigned char snapshot[2];
static int usb_error, status_error, usb_reads, status_reads;
static int regmap_read(void *map, unsigned int reg, unsigned int *value)
{
 (void)map;
 assert(reg == 0x1310);
 usb_reads++;
 if (usb_error) return usb_error;
 *value = usb;
 return 0;
}
static int regmap_bulk_read(void *map, unsigned int reg, void *value, unsigned int count)
{
 (void)map;
 assert(reg == 0x1006 && count == 2);
 status_reads++;
 if (status_error) return status_error;
 memcpy(value, snapshot, count);
 return 0;
}
/* MISEED_SMB5_STATUS_UNDER_TEST */
int main(void)
{
 struct smb5_chip chip = { .base = 0x1000 };
 /* PM8150B phase encodings 0..7, independently specified by smb5-reg.h. */
 const int expected[] = {
  POWER_SUPPLY_STATUS_NOT_CHARGING, POWER_SUPPLY_STATUS_CHARGING,
  POWER_SUPPLY_STATUS_CHARGING, POWER_SUPPLY_STATUS_CHARGING,
  POWER_SUPPLY_STATUS_CHARGING, POWER_SUPPLY_STATUS_FULL,
  POWER_SUPPLY_STATUS_NOT_CHARGING, POWER_SUPPLY_STATUS_NOT_CHARGING
 };
 int value, phase, flags, upper;

 snapshot[0] = 0x03; snapshot[1] = 0x28;
 assert(!smb5_get_prop_status(&chip, &value));
 assert(value == POWER_SUPPLY_STATUS_CHARGING);
 /* All phase/STATUS_2 combinations, with and without STATUS_1 upper bits.
  * Bit 1 overrides every phase; voltage comparator bit 5 must not do so.
  */
 for (upper = 0; upper <= 0xf8; upper += 0xf8)
  for (phase = 0; phase < 8; phase++)
   for (flags = 0; flags < 256; flags++) {
    snapshot[0] = upper | phase; snapshot[1] = flags;
    value = -1;
    assert(!smb5_get_prop_status(&chip, &value));
    assert(value == ((flags & 0x02) ?
           POWER_SUPPLY_STATUS_NOT_CHARGING : expected[phase]));
   }

 usb = 0; status_reads = 0; status_error = -EIO;
 assert(!smb5_get_prop_status(&chip, &value));
 assert(value == POWER_SUPPLY_STATUS_DISCHARGING && !status_reads);
 usb_error = -EIO; value = -123; usb_reads = 0;
 assert(smb5_get_prop_status(&chip, &value) == -EIO);
 assert(value == -123 && usb_reads == 1 && !status_reads);
 usb = 0x10; usb_error = -ETIMEDOUT;
 assert(smb5_get_prop_status(&chip, &value) == -ETIMEDOUT && value == -123);
 usb_error = 0;
 assert(smb5_get_prop_status(&chip, &value) == -EIO && value == -123);
 status_error = 0; snapshot[0] = 3; snapshot[1] = 0x2a;
 assert(!smb5_get_prop_status(&chip, &value));
 assert(value == POWER_SUPPLY_STATUS_NOT_CHARGING);
 snapshot[1] = 0x28;
 assert(!smb5_get_prop_status(&chip, &value));
 assert(value == POWER_SUPPLY_STATUS_CHARGING);
 return 0;
}
