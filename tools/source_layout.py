"""Build input inventory for the independently compiled MiSeed source units."""

# SPDX-License-Identifier: AGPL-3.0-only

POWER_GROUPS = {
    "qcom_fg": ("qcom_fg_core", "qcom_fg_io", "qcom_fg_measure"),
    "qcom_pm8150b_charger": ("qcom_pm8150b_core", "qcom_pm8150b_hw",
                              "qcom_pm8150b_policy", "qcom_pm8150b_supply",
                              "qcom_pm8150b_thermal", "qcom_pm8150b_pd", "umi_wired_driver"),
    "umi-bq25970": ("umi_pps", "umi_pps_pd", "umi_pps_hw", "umi_pps_supply",
                    "umi_pps_thermal", "umi_pps_driver",
                    "umi_bq25970", "umi_bq25970_profile"),
    "bq25970_core": ("bq25970", "bq25970_i2c", "bq25970_control",
                     "bq25970_sample", "bq25970_supply"),
}
POWER_HEADERS = (
    "qcom_fg_internal.h", "qcom_pm8150b_internal.h", "qcom_pm8150b_regs.h",
    "umi_pps_internal.h", "umi_charge_policy.h", "umi_wired_internal.h",
    "umi_smb5_diagnostics.h", "bq25970.h", "bq25970_device.h", "bq25970_internal.h", "bq25970_board.h",
    "umi_bq25970.h", "umi_pps_policy.h",
)
POWER_FILES = tuple(stem + ".c" for group in POWER_GROUPS.values() for stem in group) + POWER_HEADERS
POWER_RULES = (
    "obj-$(CONFIG_BATTERY_MISEED_QCOM_FG) += qcom_fg.o",
    "obj-$(CONFIG_CHARGER_MISEED_PM8150B) += qcom_pm8150b_charger.o",
    *(target + "-y := " + " ".join(stem + ".o" for stem in stems)
      for target, stems in POWER_GROUPS.items() if target not in ("umi-bq25970", "bq25970_core")),
    "obj-$(CONFIG_CHARGER_BQ25970_CORE) += bq25970_charger.o",
    "bq25970_charger-y := " + " ".join(stem + ".o" for stem in POWER_GROUPS["bq25970_core"]),
    "qcom_pm8150b_charger-$(CONFIG_CHARGER_MISEED_UMI_PPS) += " +
    " ".join(stem + ".o" for stem in POWER_GROUPS["umi-bq25970"]),
)
RESCUE_FILES = (
    "rescue_init.c", "rescue_fs.c", "rescue_usb_diag.c", "rescue_devices.c",
    "rescue_power.c", "rescue_nfc.c", "rescue_nfc_l3.c", "rescue_type4.c",
    "rescue_nfc_netlink.c", "rescue_usb.c", "rescue_dt.c",
    "rescue_display.c", "rescue_framebuffer.c", "rescue_thermal.c", "charge_log.c",
)
RESCUE_HEADERS = ("rescue_internal.h", "rescue_nfc_netlink.h", "charge_log.h")
SPLIT_INPUTS = {"tools/source_layout.py",
                *("kernel/" + name for name in RESCUE_FILES + RESCUE_HEADERS),
                *("kernel/power/" + name for name in POWER_FILES)}
