// SPDX-License-Identifier: GPL-2.0-only
/* One wired charger module owns the switch/pump interlock. Its platform and
 * auxiliary endpoints expose power_supply and thermal, not a vendor API.
 */
#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>

extern struct platform_driver qcom_spmi_smb5;
#if IS_ENABLED(CONFIG_CHARGER_MISEED_UMI_PPS)
extern struct i2c_driver umi_bq25970_driver;
extern struct auxiliary_driver umi_pps_driver;
#endif

static int __init umi_wired_init(void)
{
	int ret = platform_driver_register(&qcom_spmi_smb5);

	if (ret)
		return ret;
#if IS_ENABLED(CONFIG_CHARGER_MISEED_UMI_PPS)
	ret = auxiliary_driver_register(&umi_pps_driver);
	if (ret)
		goto unregister_switch;
	ret = i2c_add_driver(&umi_bq25970_driver);
	if (ret) {
		auxiliary_driver_unregister(&umi_pps_driver);
		goto unregister_switch;
	}
#endif
	return 0;
#if IS_ENABLED(CONFIG_CHARGER_MISEED_UMI_PPS)
unregister_switch:
	platform_driver_unregister(&qcom_spmi_smb5);
	return ret;
#endif
}
module_init(umi_wired_init);

static void __exit umi_wired_exit(void)
{
#if IS_ENABLED(CONFIG_CHARGER_MISEED_UMI_PPS)
	/* Stop/recover while the chip IRQ/regmap and SMB5 supplier still live. */
	auxiliary_driver_unregister(&umi_pps_driver);
	i2c_del_driver(&umi_bq25970_driver);
#endif
	platform_driver_unregister(&qcom_spmi_smb5);
}
module_exit(umi_wired_exit);
MODULE_DESCRIPTION("Umi wired charger with power_supply and thermal interfaces");
MODULE_LICENSE("GPL");
