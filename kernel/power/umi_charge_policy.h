/* SPDX-License-Identifier: GPL-2.0-only */
/* Umi limits: LineageOS 71b13e62f057a649b77fe4062feb73ee72ad609c,
 * umi-sm8250.dtsi chg-cool-th/chg-warm-th/fv-max-uv. Diagnostic current
 * cap is the upstream SMB5 USB2 SDP_CURRENT_UA, not a cell calibration.
 * SOC hysteresis follows misaka-link/umi-linux cf83073887899f485a5c606f9842c5e691ea796c.
 */
#ifndef MISEED_UMI_CHARGE_POLICY_H
#define MISEED_UMI_CHARGE_POLICY_H

#define UMI_CHARGE_COOL_DECIC 150
#define UMI_CHARGE_WARM_DECIC 450
#define UMI_CHARGE_MAX_UV 4450000
#define UMI_DIAGNOSTIC_CURRENT_UA 500000
#define UMI_CHARGE_UPPER_SOC 70
#define UMI_CHARGE_LOWER_SOC 65
#define UMI_CHARGE_TEST_SECONDS 60

static inline int umi_charge_policy(int online, int present, int good,
		int temp, int voltage, int soc, int max_uv,
		int previous, int test)
{
	if (!online || !present || !good || temp <= UMI_CHARGE_COOL_DECIC ||
	    temp > UMI_CHARGE_WARM_DECIC || voltage <= 0 ||
	    voltage >= max_uv || max_uv > UMI_CHARGE_MAX_UV ||
	    soc < 0 || soc > 100)
		return 0;
	if (test)
		return soc < 100;
	if (soc >= UMI_CHARGE_UPPER_SOC)
		return 0;
	if (soc <= UMI_CHARGE_LOWER_SOC)
		return 1;
	return previous;
}
#endif
