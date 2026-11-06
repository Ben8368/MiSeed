#!/usr/bin/env python3
"""Create an Android boot header v2 image from MiSeed-owned payloads."""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


HEADER_SIZE = 1660
PAGE = 4096
KERNEL_ADDR = 0x8000
RAMDISK_ADDR = 0x01000000
DTB_ADDR = 0x01F00000
MAX_BOOT_IMAGE_SIZE = 128 * 1024 * 1024
CMDLINE = b"console=ttyMSM0,115200n8 console=tty0 loglevel=8 keep_bootcon earlycon pd_ignore_unused clk_ignore_unused regulator_ignore_unused fbcon=font:TER16x32,rotate:3 rdinit=/init"


def read(path: Path) -> bytes:
    data = path.read_bytes()
    if not data:
        raise ValueError(f"empty input: {path}")
    return data


def align(value: int, page: int) -> int:
    return (value + page - 1) // page * page


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--template-sha256", required=True)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--ramdisk", required=True, type=Path)
    parser.add_argument("--dtb", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    template = read(args.template)
    if hashlib.sha256(template).hexdigest() != args.template_sha256.lower():
        raise SystemExit("reference boot template SHA-256 mismatch")
    if template[:8] != b"ANDROID!":
        raise SystemExit("reference template has no Android boot magic")
    header = bytearray(template[:HEADER_SIZE])
    kernel, ramdisk, dtb = read(args.kernel), read(args.ramdisk), read(args.dtb)
    page = struct.unpack_from("<I", header, 36)[0]
    version = struct.unpack_from("<I", header, 40)[0]
    if page != PAGE or version != 2 or struct.unpack_from("<I", header, 1644)[0] != HEADER_SIZE:
        raise SystemExit("expected a 4096-byte Android boot header v2 template")
    if struct.unpack_from("<I", header, 1632)[0] != 0:
        raise SystemExit("template unexpectedly contains a recovery DTBO")
    if len(ramdisk) >= DTB_ADDR - RAMDISK_ADDR:
        raise SystemExit("ramdisk overlaps the locked DTB load address")
    if len(dtb) > 0x100000:
        raise SystemExit("DTB exceeds the conservative 1 MiB size limit")

    struct.pack_into("<I", header, 8, len(kernel))
    struct.pack_into("<I", header, 12, KERNEL_ADDR)
    struct.pack_into("<I", header, 16, len(ramdisk))
    struct.pack_into("<I", header, 20, RAMDISK_ADDR)
    struct.pack_into("<I", header, 24, 0)
    struct.pack_into("<I", header, 28, 0)
    struct.pack_into("<I", header, 32, 0x100)
    struct.pack_into("<I", header, 1632, 0)
    struct.pack_into("<Q", header, 1636, 0)
    struct.pack_into("<I", header, 1644, HEADER_SIZE)
    struct.pack_into("<I", header, 1648, len(dtb))
    struct.pack_into("<Q", header, 1652, DTB_ADDR)
    header[64:576] = b"\0" * 512
    header[64:64 + len(CMDLINE)] = CMDLINE
    header[608:1632] = b"\0" * 1024
    digest = hashlib.sha1()
    for segment in (kernel, ramdisk, b"", b"", dtb):
        digest.update(segment)
        digest.update(struct.pack("<I", len(segment)))
    header[576:608] = digest.digest() + b"\0" * 12

    image = bytearray(header)
    image.extend(b"\0" * (align(len(image), PAGE) - len(image)))
    for segment in (kernel, ramdisk, dtb):
        image.extend(segment)
        image.extend(b"\0" * (align(len(image), PAGE) - len(image)))
    if len(image) > MAX_BOOT_IMAGE_SIZE:
        raise SystemExit("boot image exceeds the 128 MiB Umi boot partition limit")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    packed = args.output.read_bytes()
    if packed[:8] != b"ANDROID!" or struct.unpack_from("<I", packed, 40)[0] != 2:
        raise SystemExit("boot image read-back failed header validation")
    cursor = page
    for expected, size_offset in ((kernel, 8), (ramdisk, 16)):
        size = struct.unpack_from("<I", packed, size_offset)[0]
        if packed[cursor:cursor + size] != expected:
            raise SystemExit("boot image read-back payload mismatch")
        cursor += align(size, page)
    actual_dtb_size = struct.unpack_from("<I", packed, 1648)[0]
    if actual_dtb_size != len(dtb) or packed[cursor:cursor + actual_dtb_size] != dtb:
        raise SystemExit("boot image read-back DTB mismatch")
    if packed[64:64 + len(CMDLINE)] != CMDLINE:
        raise SystemExit("boot image read-back command line mismatch")
    print(f"boot image: {args.output} ({len(image)} bytes)")
    print(f"kernel={len(kernel)} ramdisk={len(ramdisk)} dtb={len(dtb)} page={PAGE} header=v2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
