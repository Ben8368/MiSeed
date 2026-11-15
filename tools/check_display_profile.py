"""Check the normal console profile and required fbdev capture configuration."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
from pathlib import Path

if __package__:
    from .pack_android_boot_v2 import CMDLINE
else:
    from pack_android_boot_v2 import CMDLINE


def check(config, native=False):
    lines = config.splitlines()
    keys = [line.split("=", 1)[0] for line in lines if line.startswith("CONFIG_")]
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate resolved configuration entry")
    expected = 'CONFIG_CMDLINE="' + CMDLINE.decode("ascii") + '"'
    if expected not in lines or 'CONFIG_CMDLINE_FORCE=y' not in lines:
        raise ValueError("resolved config lost the forced diagnostic command line")
    if native:
        for option in ("FB_DEVICE", "FRAMEBUFFER_CONSOLE", "DUMMY_CONSOLE", "DRM_SIMPLEDRM",
                       "DRM_PANEL_XIAOMI_UMI_CSOT", "PRINTK_TIME"):
            if "CONFIG_" + option + "=y" not in lines:
                raise ValueError("native console/capture lost CONFIG_" + option)
        if "CONFIG_LOG_BUF_SHIFT=20" not in lines:
            raise ValueError("native capture lost the 1 MiB kernel log ring")
        if any(line.startswith("CONFIG_FB_SIMPLE=") for line in lines):
            raise ValueError("legacy simplefb competes with DRM")
        if "CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER=y" in lines:
            raise ValueError("failed deferred-takeover experiment must remain disabled")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    try:
        check(args.config.read_text(), args.native)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print("PASS: display console profile and capture prerequisites")


if __name__ == "__main__":
    main()
