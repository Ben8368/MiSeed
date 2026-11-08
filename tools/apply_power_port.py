"""Stage the pinned FG/SMB5 Umi runtime integration."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
from pathlib import Path


REVISION = "f45ed76d6791cf72ff1265ee93511175bbbc2f18"
RULES = (
    "obj-$(CONFIG_BATTERY_MISEED_QCOM_FG) += qcom_fg.o",
    "obj-$(CONFIG_CHARGER_MISEED_PM8150B) += qcom_pm8150b_charger.o",
)
KCONFIG_RULE = 'source "drivers/power/supply/miseed-power.Kconfig"'


def stage(source, inputs):
    marker = source / ".miseed-source-revision"
    if marker.read_text().strip() != REVISION:
        raise ValueError("source is not the pinned Linux revision")
    target = source / "drivers/power/supply"
    for name in ("qcom_fg.c", "qcom_pm8150b_charger.c", "Kconfig", "Makefile",
                 "miseed-power.Kconfig", "umi_charge_policy.h"):
        if (target / name).is_symlink():
            raise ValueError(f"refusing symlink target: {name}")
    for name in ("qcom_fg.c", "qcom_pm8150b_charger.c", "umi_charge_policy.h"):
        (target / name).write_bytes((inputs / name).read_bytes())
    (target / "miseed-power.Kconfig").write_bytes((inputs / "Kconfig").read_bytes())
    for name, rules in (("Makefile", RULES), ("Kconfig", (KCONFIG_RULE,))):
        path = target / name
        text = path.read_text(encoding="utf-8")
        for rule in rules:
            if rule not in text.splitlines():
                text += "\n" + rule + "\n"
        path.write_text(text, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.source, Path(__file__).resolve().parents[1] / "kernel/power")
    except (OSError, ValueError) as error:
        parser.exit(1, f"power port staging failed: {error}\n")
    board = args.source / "arch/arm64/boot/dts/qcom/umi-pm8150b.dtsi"
    if board.is_symlink():
        parser.error("refusing board include symlink")
    board.write_bytes((Path(__file__).resolve().parents[1] / "kernel/power/umi-pm8150b.dtsi").read_bytes())
    print("Staged Umi FG/SMB5 runtime port; probe starts charge-inhibited")


if __name__ == "__main__":
    main()
