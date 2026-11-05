#!/usr/bin/env python3
"""Extract the DTB bytes from a hash-locked Android boot header v2 image."""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


def align(value: int, page: int) -> int:
    return (value + page - 1) // page * page


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    image = args.image.read_bytes()
    if hashlib.sha256(image).hexdigest() != args.sha256.lower():
        raise SystemExit("reference boot image SHA-256 mismatch")
    if image[:8] != b"ANDROID!" or len(image) < 1660:
        raise SystemExit("invalid Android boot image")
    page, version = struct.unpack_from("<II", image, 36)
    if page != 4096 or version != 2:
        raise SystemExit("expected Android boot header version 2 with 4096-byte pages")
    kernel_size, ramdisk_size, second_size = (
        struct.unpack_from("<I", image, 8)[0],
        struct.unpack_from("<I", image, 16)[0],
        struct.unpack_from("<I", image, 24)[0],
    )
    recovery_size = struct.unpack_from("<I", image, 1632)[0]
    dtb_size = struct.unpack_from("<I", image, 1648)[0]
    offset = page + align(kernel_size, page) + align(ramdisk_size, page) + align(second_size, page)
    offset += align(recovery_size, page)
    dtb = image[offset:offset + dtb_size]
    if not dtb_size or len(dtb) != dtb_size or dtb[:4] != b"\xd0\x0d\xfe\xed":
        raise SystemExit("reference image has an invalid or truncated v2 DTB segment")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(dtb)
    print(f"extracted metadata-only DTB segment ({len(dtb)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
