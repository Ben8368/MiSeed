#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Apply the pinned Android NQ-NCI compatibility driver to Linux source."""

import argparse
from pathlib import Path


CONFIG = '''\
config MISEED_NFC_NQ
\tbool "MiSeed NQ-NCI Android compatibility driver"
\tdepends on I2C && GPIOLIB
\thelp
\t  Build the QTI/NXP NQ-NCI character device interface used by the
\t  Android NXP NFC HAL. This driver exposes /dev/nq-nci and the
\t  downstream ioctl ABI; it does not provide an Android HAL or framework.

'''
MAKE_RULE = "obj-$(CONFIG_MISEED_NFC_NQ) += nq-nci.o"
FILES = ("nq-nci.c", "nq-nci.h", "nfcinfo-uapi.h")


def apply(root: Path, project_root: Path) -> None:
    nfc_dir = root / "drivers/nfc"
    kconfig = root / "drivers/Kconfig"
    makefile = nfc_dir / "Makefile"
    if not nfc_dir.is_dir() or not kconfig.is_file() or not makefile.is_file():
        raise SystemExit("Linux source tree is missing drivers Kconfig or drivers/nfc/Makefile")

    for name in FILES:
        source = project_root / "kernel/nfc" / name
        if not source.is_file() or source.is_symlink():
            raise SystemExit(f"missing or unsafe NFC source input: {source}")
        (nfc_dir / name).write_bytes(source.read_bytes())

    kconfig_text = kconfig.read_text(encoding="utf-8")
    if "config MISEED_NFC_NQ" not in kconfig_text:
        endmenu = kconfig_text.rfind("endmenu")
        endline = kconfig_text.find("\n", endmenu)
        if endmenu < 0 or endline < 0:
            raise SystemExit("drivers/Kconfig has no final endmenu")
        kconfig_text = (kconfig_text[:endline + 1] + "\n" + CONFIG
                        + kconfig_text[endline + 1:])
        kconfig.write_text(kconfig_text, encoding="utf-8", newline="\n")

    make_text = makefile.read_text(encoding="utf-8")
    if MAKE_RULE not in make_text.splitlines():
        makefile.write_text(make_text.rstrip("\n") + "\n" + MAKE_RULE + "\n",
                            encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--project-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    apply(args.source.resolve(), args.project_root.resolve())
    print("Applied NQ-NCI driver and /dev/nq-nci Android ABI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
