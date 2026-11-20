"""Check the SN100T and RX1619 resources in the final Umi DTB."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import subprocess
from pathlib import Path


def validate(dtb):
    def get(node, prop, kind="s"):
        return subprocess.check_output(["fdtget", "-t", kind, dtb, node, prop], text=True).strip()

    nfc, rx, bus, gpio, charger = (get("/__symbols__", symbol) for symbol in
                                  ("umi_nfc", "umi_wireless_rx", "i2c16", "pm8150b_gpios", "umi_charger"))
    if rx != bus + "/charger@61" or bus.rsplit("/", 1)[-1] != "i2c@888000":
        raise ValueError("RX1619 is not on QUP16 at address 0x61")
    rx_handle = get(rx, "phandle", "x")
    tlmm = get("/__symbols__", "miseed_live_tlmm")
    pg = get("/__symbols__", "rx1619_power_good_default")
    if not pg.startswith(gpio + "/"):
        raise ValueError("RX1619 power-good pinctrl is not under PM8150B GPIO")
    pg_props = subprocess.check_output(["fdtget", "-p", dtb, pg], text=True).split()
    if set(pg_props) != {"pins", "function", "power-source", "bias-pull-down",
                         "input-enable", "phandle"}:
        raise ValueError("RX1619 power-good pinctrl must only configure an input")
    checks = (
        (nfc, "compatible", "s", "nxp,sn100t"),
        (nfc, "reg", "x", "28"),
        (nfc, "interrupts", "x", "6f 4"),
        (bus, "status", "s", "okay"),
        (rx, "compatible", "s", "xiaomi,umi-rx1619"),
        (rx, "reg", "x", "61"),
        (rx, "pinctrl-names", "s", "default"),
        (rx, "pinctrl-0", "x", get(pg, "phandle", "x")),
        (pg, "pins", "s", "gpio7"),
        (pg, "function", "s", "normal"),
        (pg, "power-source", "x", "0"),
        (rx, "enable-gpios", "x", get(tlmm, "phandle", "x") + " 9b 1"),
        (rx, "power-good-gpios", "x", get(gpio, "phandle", "x") + " 7 0"),
        (nfc, "nxp,rf-interference-source", "x", rx_handle),
        (charger, "qcom,wireless-rx", "x", rx_handle),
    )
    for node, prop, kind, expected in checks:
        if get(node, prop, kind) != expected:
            raise ValueError(f"{node} {prop} does not match {expected}")
    for prop, pin in (("enable-gpios", "53"), ("firmware-gpios", "6e")):
        if get(nfc, prop, "x").split()[1:2] != [pin]:
            raise ValueError(f"{nfc} {prop} does not use GPIO {pin}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path)
    args = parser.parse_args()
    try:
        validate(args.dtb)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Umi RF binding check failed: {error}\n")


if __name__ == "__main__":
    main()
