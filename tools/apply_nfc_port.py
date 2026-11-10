#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Stage the SN100T Linux NCI transport in the pinned kernel tree."""
import argparse
import hashlib
import stat
from pathlib import Path

REVISION = "f45ed76d6791cf72ff1265ee93511175bbbc2f18"
RULE = "obj-$(CONFIG_NFC_SN100X) += sn100x.o"
KCONFIG_RULE = 'source "drivers/nfc/sn100x.Kconfig"'
LEGACY_CONFIG = 'config MISEED_NFC_NQ\n\tbool "MiSeed NQ-NCI Android compatibility driver"\n\tdepends on I2C && GPIOLIB\n\thelp\n\t  Build the QTI/NXP NQ-NCI character device interface used by the\n\t  Android NXP NFC HAL. This driver exposes /dev/nq-nci and the\n\t  downstream ioctl ABI; it does not provide an Android HAL or framework.\n\n'
LEGACY_HASHES = {
    "nq-nci.c": "2d89abff8dc325b189b83dc26d4d524a8106486c2509838661162f5b651d8bbc",
    "nq-nci.h": "8997712b2848ae6831b4dcfd08fa9382a567af67303db2510b2bf36e0329b0dd",
    "nfcinfo-uapi.h": "f60bbb0f295b26f6263fb59dda9365d48009c868687539ff76a7248c0607fee5",
}


def safe(root, name, required=False):
    path = root
    for part in Path(name).parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f"symlink path: {path}")
    if path.exists() and not stat.S_ISREG(path.stat().st_mode):
        raise ValueError(f"not a regular file: {path}")
    if required and not path.is_file():
        raise ValueError(f"missing input: {path}")
    return path


def apply(root: Path, project_root: Path) -> None:
    if root.is_symlink() or project_root.is_symlink():
        raise ValueError("symlink root")
    if safe(root, ".miseed-source-revision", True).read_text().strip() != REVISION:
        raise ValueError("source is not the pinned Linux revision")
    # Preflight all paths/content before changing any file.
    mapping = {
        "kernel/nfc/sn100x.c": "drivers/nfc/sn100x.c",
        "kernel/nfc/Kconfig": "drivers/nfc/sn100x.Kconfig",
        "kernel/nfc/bindings/nxp,sn100t.yaml":
            "Documentation/devicetree/bindings/net/nfc/nxp,sn100t.yaml",
    }
    copies = [(safe(root, dst), safe(project_root, src, True).read_bytes())
              for src, dst in mapping.items()]
    for dst, _ in copies:
        if not dst.parent.is_dir():
            raise ValueError(f"missing destination: {dst.parent}")
    top = safe(root, "drivers/Kconfig", True)
    kconfig = safe(root, "drivers/nfc/Kconfig", True)
    makefile = safe(root, "drivers/nfc/Makefile", True)
    top_text = top.read_text(encoding="utf-8")
    if "config MISEED_NFC_NQ" in top_text and top_text.count(LEGACY_CONFIG) != 1:
        raise ValueError("legacy NFC Kconfig changed; refusing removal")
    kt = kconfig.read_text(encoding="utf-8")
    if KCONFIG_RULE not in kt.splitlines():
        if kt.count("endmenu") != 1:
            raise ValueError("NFC Kconfig menu changed")
        kt = kt.replace("endmenu", KCONFIG_RULE + "\nendmenu")
    mt = makefile.read_text(encoding="utf-8")
    mt = mt.replace("obj-$(CONFIG_MISEED_NFC_NQ) += nq-nci.o\n", "")
    if RULE not in mt.splitlines():
        mt = mt.rstrip("\n") + "\n" + RULE + "\n"
    obsolete = []
    for name, digest in LEGACY_HASHES.items():
        path = safe(root, "drivers/nfc/" + name)
        if path.exists():
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError(f"legacy NFC input changed: {name}")
            obsolete.append(path)
    for dst, content in copies:
        dst.write_bytes(content)
    for path, content in ((top, top_text.replace(LEGACY_CONFIG, "")),
                          (kconfig, kt), (makefile, mt)):
        path.write_text(content, encoding="utf-8", newline="\n")
    for path in obsolete:
        path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--project-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        apply(args.source, args.project_root)
    except (OSError, ValueError) as error:
        parser.exit(1, f"NFC staging failed: {error}\n")
    print("Staged SN100T Linux NFC/NCI; no private character ABI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
