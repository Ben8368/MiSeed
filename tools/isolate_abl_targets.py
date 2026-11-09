#!/usr/bin/env python3
"""Isolate vendor overlay targets without changing compiled mainline phandles."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

TARGETS = ("soc", "spmi_bus", "tlmm")


def get(dtb, node, prop):
    return subprocess.check_output(
        ["fdtget", "-t", "s", str(dtb), node, prop], text=True).strip()


def put(dtb, node, prop, value):
    subprocess.run(["fdtput", "-t", "s", str(dtb), node, prop, value], check=True)


def isolate(dtb):
    dtb = Path(dtb)
    with tempfile.TemporaryDirectory(dir=dtb.parent) as directory:
        staged = Path(directory) / dtb.name
        shutil.copy2(dtb, staged)
        _isolate(staged)
        os.replace(staged, dtb)


def _isolate(dtb):
    # Validate the complete plan before writing anything; refuse unknown DTBs.
    plan = []
    for name in TARGETS:
        live = get(dtb, "/__symbols__", name)
        sink = get(dtb, "/__symbols__", "miseed_dtbo_" + name)
        if not live.startswith("/soc") or not sink.startswith("/abl_ovf/isolated-"):
            raise ValueError(f"unexpected target paths for {name}: {live}, {sink}")
        if get(dtb, sink, "status") != "disabled":
            raise ValueError(f"overlay quarantine is not disabled: {name}")
        plan.append((name, live, sink))
    for name, live, sink in plan:
        put(dtb, "/__symbols__", "miseed_live_" + name, live)
        put(dtb, "/__symbols__", name, sink)
    verify(dtb)


def verify(dtb):
    if get(dtb, "/abl_ovf", "status") != "disabled":
        raise ValueError("overlay container must be disabled")
    for name in TARGETS:
        live = get(dtb, "/__symbols__", "miseed_live_" + name)
        sink = get(dtb, "/__symbols__", "miseed_dtbo_" + name)
        if not live.startswith("/soc") or not sink.startswith("/abl_ovf/isolated-"):
            raise ValueError(f"invalid isolation identity: {name}")
        if get(dtb, "/__symbols__", name) != sink or get(dtb, sink, "status") != "disabled":
            raise ValueError(f"vendor target escaped isolation: {name}")
    spmi = get(dtb, "/__symbols__", "miseed_live_spmi_bus")
    children = subprocess.check_output(["fdtget", "-l", str(dtb), spmi], text=True).split()
    if sorted(children) != [f"pmic@{i}" for i in ("0", "1", "2", "3", "4", "5", "a", "b")]:
        raise ValueError(f"unexpected live SPMI children: {children}")
    tlmm = get(dtb, "/__symbols__", "miseed_live_tlmm")
    if get(dtb, tlmm, "status") != "okay":
        raise ValueError("allowlisted Umi TLMM is not enabled")
    ranges = subprocess.check_output(
        ["fdtget", "-t", "u", str(dtb), tlmm, "gpio-reserved-ranges"], text=True).split()
    if ranges != ["0", "34", "36", "8", "46", "22", "69", "112"]:
        raise ValueError("TLMM must expose only Umi UART12, I2C15, and BQ IRQ GPIOs")
    print("verified isolated soc/SPMI/TLMM overlay targets; live phandles retained")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    (verify if args.verify else isolate)(args.dtb)


if __name__ == "__main__":
    main()
