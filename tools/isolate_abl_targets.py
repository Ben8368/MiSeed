#!/usr/bin/env python3
"""Isolate vendor overlay targets without changing compiled mainline phandles."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

TARGETS = ("soc", "spmi_bus", "tlmm", "mdss_mdp", "sdhc_2")


def get(dtb, node, prop):
    return subprocess.check_output(
        ["fdtget", "-t", "s", str(dtb), node, prop], text=True).strip()


def put(dtb, node, prop, value):
    subprocess.run(["fdtput", "-t", "s", str(dtb), node, prop, value], check=True)


def verify_native_reset(dtb):
    mdss = get(dtb, "/__symbols__", "mdss")
    dispcc = get(dtb, "/__symbols__", "dispcc")
    if (get(dtb, mdss, "compatible") != "qcom,sm8250-mdss" or
            get(dtb, dispcc, "compatible") != "qcom,sm8250-dispcc"):
        raise ValueError("unexpected native MDSS/reset provider")
    def cells(node, prop):
        return subprocess.check_output(
            ["fdtget", "-t", "x", str(dtb), node, prop], text=True).split()
    # Locked qcom,dispcc-sm8250.h: MDSS_CORE_BCR=0, RSCC_BCR=1.
    if (cells(dispcc, "#reset-cells") != ["1"] or
            cells(mdss, "resets") != cells(dispcc, "phandle") + ["0"]):
        raise ValueError("native MDSS must use DISP_CC_MDSS_CORE_BCR")


def isolate(dtb, native=False):
    dtb = Path(dtb)
    with tempfile.TemporaryDirectory(dir=dtb.parent) as directory:
        staged = Path(directory) / dtb.name
        shutil.copy2(dtb, staged)
        _isolate(staged, native)
        os.replace(staged, dtb)


def _isolate(dtb, native=False):
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
    verify(dtb, native)


def verify(dtb, native=False):
    if native:
        verify_native_reset(dtb)
    if get(dtb, "/abl_ovf", "status") != "disabled":
        raise ValueError("overlay container must be disabled")
    for name in TARGETS:
        live = get(dtb, "/__symbols__", "miseed_live_" + name)
        sink = get(dtb, "/__symbols__", "miseed_dtbo_" + name)
        if not live.startswith("/soc") or not sink.startswith("/abl_ovf/isolated-"):
            raise ValueError(f"invalid isolation identity: {name}")
        guard = sink.rsplit("/", 1)[0] if name in ("mdss_mdp", "sdhc_2") else sink
        if get(dtb, "/__symbols__", name) != sink or get(dtb, guard, "status") != "disabled":
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
    expected = (["0", "4", "6", "1", "8", "4", "13", "21", "36", "8",
                 "46", "22", "69", "14", "84", "26", "112", "69"] if native else
                ["0", "4", "6", "1", "8", "26", "36", "8",
                 "46", "22", "69", "14", "84", "26", "112", "69"])
    if ranges != expected:
        raise ValueError(
            "TLMM must expose only Umi UART12, I2C15, BQ IRQ, and NQ NFC GPIOs")
    print("verified isolated soc/SPMI/TLMM/DPU/MMC targets; live phandles retained")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    (verify if args.verify else isolate)(args.dtb, args.native)


if __name__ == "__main__":
    main()
