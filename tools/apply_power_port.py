"""Stage the pinned FG/SMB5 Umi runtime integration."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import stat
from pathlib import Path


REVISION = "f45ed76d6791cf72ff1265ee93511175bbbc2f18"
RULES = (
    "obj-$(CONFIG_BATTERY_MISEED_QCOM_FG) += qcom_fg.o",
    "obj-$(CONFIG_CHARGER_MISEED_PM8150B) += qcom_pm8150b_charger.o",
    "obj-$(CONFIG_CHARGER_MISEED_UMI_PPS) += umi_pps.o",
)
KCONFIG_RULE = 'source "drivers/power/supply/miseed-power.Kconfig"'
# rpmhpd_probe() reads OPP tables while registering its genpd provider.
RPMHPD_KCONFIG_RULE = (
    'config QCOM_RPMHPD\n'
    '\ttristate "Qualcomm RPMh Power domain driver"\n'
    '\tdepends on QCOM_RPMH && QCOM_COMMAND_DB\n'
    '\tselect PM_OPP'
)


def destination_dir(source, relative):
    source = Path(source)
    current = source
    for component in Path(relative).parts:
        current = current / component
        if current.is_symlink():
            raise ValueError(f"refusing symlink directory: {current.relative_to(source)}")
        if not current.is_dir():
            raise ValueError(f"destination directory is missing: {current.relative_to(source)}")
    return current


def stage(source, inputs):
    source = Path(source)
    if source.is_symlink() or not source.is_dir():
        raise ValueError("source must be a regular directory")

    marker = source / ".miseed-source-revision"
    if not stat.S_ISREG(marker.lstat().st_mode):
        raise ValueError("source revision marker must be a regular file")
    if marker.read_text().strip() != REVISION:
        raise ValueError("source is not the pinned Linux revision")

    target = destination_dir(source, "drivers/power/supply")
    for name in ("qcom_fg.c", "qcom_pm8150b_charger.c", "Kconfig", "Makefile",
                 "miseed-power.Kconfig", "umi_charge_policy.h", "umi_smb5.h",
                 "umi_pps.c", "umi_pps_policy.h"):
        if (target / name).is_symlink():
            raise ValueError(f"refusing symlink target: {name}")
    for name in ("qcom_fg.c", "qcom_pm8150b_charger.c", "umi_charge_policy.h",
                 "umi_smb5.h", "umi_pps.c", "umi_pps_policy.h"):
        (target / name).write_bytes((inputs / name).read_bytes())
    (target / "miseed-power.Kconfig").write_bytes((inputs / "Kconfig").read_bytes())
    for name, rules in (("Makefile", RULES), ("Kconfig", (KCONFIG_RULE,))):
        path = target / name
        text = path.read_text(encoding="utf-8")
        for rule in rules:
            if rule not in text.splitlines():
                text += "\n" + rule + "\n"
        path.write_text(text, encoding="utf-8", newline="\n")

    rpmhpd_kconfig = destination_dir(source, "drivers/pmdomain/qcom") / "Kconfig"
    if rpmhpd_kconfig.is_symlink():
        raise ValueError("refusing RPMh power-domain Kconfig symlink")
    text = rpmhpd_kconfig.read_text(encoding="utf-8")
    rule = RPMHPD_KCONFIG_RULE
    base_rule = rule.removesuffix("\n\tselect PM_OPP")
    if rule not in text:
        if text.count(base_rule) != 1:
            raise ValueError("pinned QCOM_RPMHPD Kconfig stanza changed")
        text = text.replace(base_rule, rule, 1)
        rpmhpd_kconfig.write_text(text, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.source, Path(__file__).resolve().parents[1] / "kernel/power")
    except (OSError, ValueError) as error:
        parser.exit(1, f"power port staging failed: {error}\n")
    try:
        board_dir = destination_dir(args.source, "arch/arm64/boot/dts/qcom")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for name in ("umi-pm8150b.dtsi", "umi-pps.dtsi"):
        if (board_dir / name).is_symlink():
            parser.error("refusing board include symlink")
    for name in ("umi-pm8150b.dtsi", "umi-pps.dtsi"):
        (board_dir / name).write_bytes(
            (Path(__file__).resolve().parents[1] / "kernel/power" / name).read_bytes())
    print("Staged Umi FG/SMB5 runtime port; probe starts charge-inhibited")


if __name__ == "__main__":
    main()
