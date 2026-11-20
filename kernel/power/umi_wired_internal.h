/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MISEED_UMI_WIRED_INTERNAL_H
#define MISEED_UMI_WIRED_INTERNAL_H
/* Private to qcom_pm8150b_charger.o; never exported to another module. */
struct power_supply;
/* claimed remains true on a post-claim I/O error; unwind before release. */
int umi_smb5_claim(struct power_supply *psy, int *stage, bool *claimed);
bool umi_smb5_wired_detached(struct power_supply *psy);
int umi_smb5_wireless_rx_state(struct power_supply *psy);
/* Carry the validated SOC stage while ownership still excludes SMB5 charging. */
int umi_smb5_check(struct power_supply *psy, int stage);
int umi_smb5_release(struct power_supply *psy, int fault);
#endif
