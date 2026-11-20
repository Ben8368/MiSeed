// SPDX-License-Identifier: GPL-2.0-only
/* This option is for an evidence-only RAM image. Control writes only stop
 * battery charging. The CC read follows the pinned Qualcomm TCPC driver.
 */
#include "qcom_pm8150b_internal.h"

#define PM8150B_TYPEC_BASE	0x1500
#define TYPEC_MISC_STATUS	0x0b
#define TYPEC_CC_ATTACHED	BIT(0)

int smb5_rx_diag_check_off(struct smb5_chip *chip)
{
	int state, ret;

	ret = smb5_get_prop_charging_enabled(chip, &state);
	if (ret)
		return ret;
	return state == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE ? 0 : -EBUSY;
}

int smb5_rx_diag_stop(struct smb5_chip *chip)
{
	int ret = smb5_inhibit_with_fallback(chip, "rx-diagnostic");
	int suspend_ret;

	if (ret)
		return ret;
	ret = smb5_rx_diag_check_off(chip);
	if (!ret)
		return 0;

	/* A lost or contradictory readback must also stop input if possible. */
	suspend_ret = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
	dev_err(chip->dev, "miseed-rx-diag smb5 stop unverified rc=%d suspend_rc=%d\n",
		ret, suspend_ret);
	return ret;
}

static int smb5_rx_diag_read_cc(struct smb5_chip *chip, unsigned int *status)
{
	struct device *tcpc = chip->pd->dev.parent;
	u32 base;
	int ret;

	if (!tcpc || tcpc->parent != chip->dev->parent ||
	    !device_is_compatible(tcpc, "qcom,pm8150b-typec"))
		return -ENODEV;
	ret = device_property_read_u32(tcpc, "reg", &base);
	if (ret || base != PM8150B_TYPEC_BASE)
		return ret ? ret : -EINVAL;
	return regmap_read(chip->regmap, base + TYPEC_MISC_STATUS, status);
}

/* Called with the charger lock. An unexpected enable is recorded even when
 * the subsequent stop succeeds, so the diagnostic session cannot pass.
 */
void smb5_rx_diag_sample(struct smb5_chip *chip)
{
	unsigned int cc;
	int charge_ret = smb5_rx_diag_check_off(chip);
	int cc_ret = smb5_rx_diag_read_cc(chip, &cc);

	if (charge_ret) {
		int stop_ret = smb5_rx_diag_stop(chip);

		dev_err_ratelimited(chip->dev,
				    "miseed-rx-diag smb5 readback failed rc=%d stop_rc=%d\n",
				    charge_ret, stop_ret);
	}
	if (cc_ret)
		dev_info(chip->dev, "miseed-rx-diag smb5_off=%u cc_error=%d\n",
			 !charge_ret, cc_ret);
	else
		dev_info(chip->dev, "miseed-rx-diag smb5_off=%u cc_attached=%u cc_status=0x%02x\n",
			 !charge_ret, !!(cc & TYPEC_CC_ATTACHED), cc);
}
