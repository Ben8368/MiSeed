/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MISEED_UMI_SMB5_H
#define MISEED_UMI_SMB5_H
struct power_supply;
/* Exclusive ownership; a successful release resumes the SMB5 policy worker. */
int umi_smb5_claim(struct power_supply *psy);
/* Carry the validated SOC stage while ownership still excludes SMB5 charging. */
int umi_smb5_check(struct power_supply *psy, int stage);
int umi_smb5_release(struct power_supply *psy);
#endif
