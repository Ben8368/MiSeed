#!/usr/bin/env python3
"""Build a deterministic, minimal newc/gzip initramfs using Python stdlib."""
from __future__ import annotations

import argparse
import gzip
import stat
import struct
from pathlib import Path


def record(name: str, mode: int, inode: int, data: bytes = b"", major: int = 0, minor: int = 0) -> bytes:
    encoded = name.encode("utf-8") + b"\0"
    nlink = 2 if stat.S_ISDIR(mode) else 1
    fields = (inode, mode, 0, 0, nlink, 0, len(data), 0, 0, major, minor, len(encoded), 0)
    out = b"070701" + b"".join(f"{item:08x}".encode("ascii") for item in fields)
    out += encoded
    out += b"\0" * (-len(out) % 4)
    out += data
    out += b"\0" * (-len(out) % 4)
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--init", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not args.init.is_file():
        parser.error("init binary does not exist")
    entries = [
        ("dev", stat.S_IFDIR | 0o755, b"", 0, 0),
        ("proc", stat.S_IFDIR | 0o555, b"", 0, 0),
        ("sys", stat.S_IFDIR | 0o555, b"", 0, 0),
        ("dev/console", stat.S_IFCHR | 0o600, b"", 5, 1),
        ("dev/null", stat.S_IFCHR | 0o666, b"", 1, 3),
        ("init", stat.S_IFREG | 0o755, args.init.read_bytes(), 0, 0),
    ]
    raw = b"".join(record(name, mode, ino, data, major, minor)
                   for ino, (name, mode, data, major, minor) in enumerate(entries, 1))
    raw += record("TRAILER!!!", 0, len(entries) + 1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0, compresslevel=9) as gz:
            gz.write(raw)
    print(f"initramfs: {args.output} ({args.output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
