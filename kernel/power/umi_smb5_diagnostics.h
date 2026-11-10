// SPDX-License-Identifier: GPL-2.0-only
/* MiSeed Umi read-only register diagnostics, called with the SMB5 lock held.
 * Register layout: Qualcomm/The Linux Foundation smb5-reg.h and qcom_smbx.c;
 * see NOTICE.md. No register writes, IRQ acknowledgements or extra worker.
 * Product consumers use power_supply, independently of this option.
 */
#ifndef MISEED_SMB5_DIAGNOSTICS_H
#define MISEED_SMB5_DIAGNOSTICS_H

#ifdef CONFIG_MISEED_POWER_DIAGNOSTICS
#define OBS_CHGR_STATUS1 0x006
#define OBS_CHGR_STATUS2 0x007
#define OBS_CHGR_ENABLE  0x042
#define OBS_USB_RT      0x310
#define OBS_USB_PLUGIN  BIT(4)

static void smb5_log_registers(struct smb5_chip *chip)
{
	static const char * const phases[] = {
		"inhibit", "trickle", "precharge", "fullon",
		"taper", "terminate", "pause", "disabled",
	};
	static const unsigned int addresses[] = {
		OBS_USB_RT, OBS_CHGR_STATUS1, OBS_CHGR_STATUS2, OBS_CHGR_ENABLE,
	};
	unsigned int values[ARRAY_SIZE(addresses)];
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(addresses); i++) {
		rc = regmap_read(chip->regmap, chip->base + addresses[i], &values[i]);
		if (rc < 0) {
			dev_info(chip->dev, "miseed-power read_error reg=0x%04x rc=%d\n",
				 chip->base + addresses[i], rc);
			return;
		}
	}

	/* Hardware indications only; phase/enable alone do not prove net charge. */
	dev_info(chip->dev,
		 "miseed-power usb_present=%u hw_phase=%s chg_enable=%u usb_rt=0x%02x chgr1=0x%02x chgr2=0x%02x control=read-only\n",
		 !!(values[0] & OBS_USB_PLUGIN), phases[values[1] & GENMASK(2, 0)],
		 !!(values[3] & BIT(0)), values[0], values[1], values[2]);
}

#else
static inline void smb5_log_registers(struct smb5_chip *chip) { }
#endif
#endif
