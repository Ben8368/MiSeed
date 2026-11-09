/* SPDX-License-Identifier: GPL-2.0-only */
/* LOS Umi smb5 limits: LineageOS 71b13e62f057a649b77fe4062feb73ee72ad609c,
 * umi-sm8250.dtsi usb-icl-ua/fcc-max-ua/fv-max-uv/chg-cool-th/chg-warm-th.
 */
#ifndef MISEED_UMI_CHARGE_POLICY_H
#define MISEED_UMI_CHARGE_POLICY_H

#define UMI_CHARGE_COOL_DECIC 150
#define UMI_CHARGE_WARM_DECIC 450
#define UMI_CHARGE_MAX_UV 4450000
#define UMI_CHARGE_USB_ICL_UA 3000000
#define UMI_CHARGE_FCC_UA 6000000
#define UMI_CHARGE_END_SOC 100
#define UMI_CHARGE_START_SOC 0
#define UMI_CHARGE_TEST_SECONDS 60
#define UMI_CHARGE_EXTENDED_TEST_SECONDS 600

static inline int umi_charge_policy(int online, int present, int good,
		int temp, int voltage, int soc, int max_uv,
		int previous, int test)
{
	(void)previous;
	(void)test;
	if (!online || !present || !good || temp <= UMI_CHARGE_COOL_DECIC ||
	    temp > UMI_CHARGE_WARM_DECIC || voltage <= 0 ||
	    max_uv > UMI_CHARGE_MAX_UV ||
	    soc < 0 || soc > 100)
		return 0;
	/* SMB5 hardware regulates/terminates at the verified programmed FV.
	 * Do not turn a transient BMS voltage sample into repeated global stops.
	 * LOS Umi uses staged FV/FCC votes rather than this software cutoff.
	 */
	return 1;
}
#endif
