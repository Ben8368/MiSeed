"""Build input inventory for the independently compiled MiSeed source units."""

# SPDX-License-Identifier: AGPL-3.0-only

POWER_GROUPS = {
    "qcom_fg": ("qcom_fg_core", "qcom_fg_io", "qcom_fg_measure"),
    "qcom_pm8150b_charger": ("qcom_pm8150b_core", "qcom_pm8150b_hw",
                              "qcom_pm8150b_policy", "qcom_pm8150b_supply",
                              "qcom_pm8150b_thermal", "qcom_pm8150b_pd"),
    "umi-bq25970": ("umi_pps", "umi_pps_pd", "umi_pps_hw", "umi_pps_supply",
                    "umi_pps_thermal", "umi_pps_driver"),
    "bq25970_core": ("bq25970", "bq25970_i2c"),
}
POWER_HEADERS = (
    "qcom_fg_internal.h", "qcom_pm8150b_internal.h", "qcom_pm8150b_regs.h",
    "umi_pps_internal.h", "umi_charge_policy.h", "umi_smb5.h",
    "umi_smb5_diagnostics.h", "bq25970.h", "bq25970_device.h", "umi_pps_policy.h",
)
POWER_FILES = tuple(stem + ".c" for group in POWER_GROUPS.values() for stem in group) + POWER_HEADERS
POWER_RULES = (
    "obj-$(CONFIG_BATTERY_MISEED_QCOM_FG) += qcom_fg.o",
    "obj-$(CONFIG_CHARGER_MISEED_PM8150B) += qcom_pm8150b_charger.o",
    "obj-$(CONFIG_CHARGER_MISEED_UMI_PPS) += umi-bq25970.o",
    "obj-$(CONFIG_CHARGER_BQ25970_CORE) += bq25970_core.o",
    *(target + "-y := " + " ".join(stem + ".o" for stem in stems)
      for target, stems in POWER_GROUPS.items()),
)
RESCUE_FILES = (
    "rescue_init.c", "rescue_fs.c", "rescue_usb_diag.c", "rescue_devices.c",
    "rescue_power.c", "rescue_nfc.c", "rescue_nfc_l3.c",
    "rescue_nfc_netlink.c", "rescue_usb.c", "rescue_dt.c",
    "rescue_display.c", "rescue_thermal.c", "charge_log.c",
)
RESCUE_HEADERS = ("rescue_internal.h", "rescue_nfc_netlink.h", "charge_log.h")
SPLIT_INPUTS = {"tools/source_layout.py",
                *("kernel/" + name for name in RESCUE_FILES + RESCUE_HEADERS),
                *("kernel/power/" + name for name in POWER_FILES)}
