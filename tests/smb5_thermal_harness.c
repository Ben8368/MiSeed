// SPDX-License-Identifier: AGPL-3.0-only
/* Synthetic tests; no physical I/O or new board calibration values. */
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

struct smb5_chip {
	int lock, status_change_work, bms;
	bool stopping, pps_owned, enabled, inhibit;
	unsigned long cooling_state;
};
struct thermal_zone_device { struct smb5_chip *data; };
struct thermal_cooling_device { struct smb5_chip *devdata; };
union power_supply_propval { int intval; };
#define POWER_SUPPLY_PROP_TEMP 1
#define mutex_lock(lock) ((void)(lock))
#define mutex_unlock(lock) ((void)(lock))
#define thermal_zone_device_priv(zone) ((zone)->data)
static int temperature = 300, read_error, stop_error, stops, schedules;
static int power_supply_get_property(int bms, int property, union power_supply_propval *v)
{
	assert(property == POWER_SUPPLY_PROP_TEMP);
	if (read_error)
		return read_error;
	v->intval = temperature;
	return 0;
}
static int smb5_inhibit_with_fallback(struct smb5_chip *chip, const char *why)
{
	stops++;
	return stop_error;
}
#define system_wq 0
#define mod_delayed_work(queue,work,delay) (++schedules)
/* DRIVER_UNDER_TEST */

int main(void)
{
	struct smb5_chip chip = {0};
	struct thermal_zone_device zone = { &chip };
	struct thermal_cooling_device cooling = { &chip };
	unsigned long state;
	int value = -123;

	assert(!smb5_thermal_get_temp(&zone, &value) && value == 30000);
	temperature = -399;
	assert(!smb5_thermal_get_temp(&zone, &value) && value == -39900);
	int invalid[] = {-400, 1250, INT_MIN, INT_MAX};
	for (unsigned int i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		temperature = invalid[i]; value = -123;
		assert(smb5_thermal_get_temp(&zone, &value) == -ERANGE && value == -123);
	}
	read_error = -EIO;
	assert(smb5_thermal_get_temp(&zone, &value) == -EIO && value == -123);
	read_error = 0;
	assert(!smb5_thermal_get_max(&cooling, &state) && state == 1);
	assert(smb5_thermal_set_state(&cooling, 2) == -EINVAL && !stops && !schedules);
	chip.enabled = true; stop_error = -EIO;
	assert(smb5_thermal_set_state(&cooling, 1) == -EIO);
	assert(chip.cooling_state == 1 && !chip.enabled && stops == 1 && schedules == 1);
	assert(!smb5_thermal_get_state(&cooling, &state) && state == 1);
	stop_error = 0;
	assert(!smb5_thermal_set_state(&cooling, 1) && stops == 2); /* Retry failed stop. */
	chip.inhibit = true;
	assert(!smb5_thermal_set_state(&cooling, 0));
	assert(chip.inhibit && !chip.enabled && stops == 2 && chip.cooling_state == 0);
	chip.pps_owned = true;
	assert(!smb5_thermal_set_state(&cooling, 1));
	assert(chip.pps_owned && chip.cooling_state == 1 && stops == 2);
	chip.pps_owned = false; /* PPS owner safely completes release. */
	assert(!smb5_thermal_set_state(&cooling, 1) && stops == 3);
	chip.stopping = true;
	assert(smb5_thermal_get_temp(&zone, &value) == -ENODEV);
	int old_schedules = schedules;
	assert(smb5_thermal_set_state(&cooling, 0) == -ENODEV);
	assert(chip.cooling_state == 1 && schedules == old_schedules && stops == 3);
	return 0;
}
