"""Backport the upstream MSM system-memory fbdev flag, native profile only."""
# SPDX-License-Identifier: GPL-2.0-only
# Upstream: Dmitry Baryshkov, ea9dadeac79cef509a4b8b4a3e3b39a741e63313.
# The staged Linux file retains its Red Hat / Rob Clark copyright header.
import hashlib

FBDEV_PATH = "drivers/gpu/drm/msm/msm_fbdev.c"
FBDEV_SHA256 = "eac2e61de75789f513f58d7ec00fd574f920d8dad6d7e35c4558b95fe9579822"
FBDEV_BEFORE = "\tfbi->fbops = &msm_fb_ops;\n"
FBDEV_AFTER = FBDEV_BEFORE + "\tfbi->flags |= FBINFO_VIRTFB; /* system memory */\n"


def transform_fbdev(original):
    if hashlib.sha256(original).hexdigest() != FBDEV_SHA256:
        raise ValueError("MSM fbdev source differs from the pinned Linux baseline")
    text = original.decode("utf-8")
    if text.count(FBDEV_BEFORE) != 1:
        raise ValueError("unexpected MSM fbdev patch anchors")
    return text.replace(FBDEV_BEFORE, FBDEV_AFTER).encode("utf-8")


def stage_fbdev(source, native=True):
    path = source / FBDEV_PATH
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError("symlink MSM fbdev target")
        if parent == source:
            break
    current = path.read_bytes()
    original = current.replace(FBDEV_AFTER.encode(), FBDEV_BEFORE.encode())
    patched = transform_fbdev(original)
    if current not in (original, patched):
        raise ValueError("unexpected MSM fbdev local modifications")
    path.write_bytes(patched if native else original)
