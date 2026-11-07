"""Stage the local read-only Umi power observer in the pinned Linux tree."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
from pathlib import Path


REVISION = "f45ed76d6791cf72ff1265ee93511175bbbc2f18"
RULE = "obj-$(CONFIG_MFD_SPMI_PMIC) += miseed_power_observer.o"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    marker = args.source / ".miseed-source-revision"
    if not marker.is_file() or marker.read_text().strip() != REVISION:
        parser.error("source is not the pinned Linux revision")
    target = args.source / "drivers/mfd"
    makefile = target / "Makefile"
    text = makefile.read_text(encoding="utf-8")
    if RULE not in text.splitlines():
        makefile.write_text(text + "\n" + RULE + "\n", encoding="utf-8", newline="\n")
    local = Path(__file__).resolve().parents[1] / "kernel/power_observer.c"
    (target / "miseed_power_observer.c").write_bytes(local.read_bytes())
    print("Staged read-only PM8150B status observer; no charger/FG control driver")


if __name__ == "__main__":
    main()
