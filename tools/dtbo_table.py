"""Inspect an Android DTBO table and its selected root metadata, read-only."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

if __package__:
    from .check_runtime_dt import parse
else:
    from check_runtime_dt import parse

MAX_IMAGE = 64 * 1024 * 1024


def selected_indices(text):
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)(?:,(?:0|[1-9][0-9]*))*", text):
        raise ValueError("invalid DTBO selection indices")
    result = [int(n) for n in text.split(",")]
    if len(result) > 32 or len(set(result)) != len(result):
        raise ValueError("too many or repeated DTBO indices")
    return result


def table_entries(blob):
    if not 32 <= len(blob) <= MAX_IMAGE:
        raise ValueError("DTBO image outside size limit")
    magic, total, header, stride, count, offset, page, version = struct.unpack_from(">8I", blob)
    if magic != 0xd7b7ab1e or version != 0 or header != 32 or not 32 <= stride <= 256:
        raise ValueError("unsupported DTBO table identity/layout")
    table_end = offset + stride * count
    if not 0 < count <= 4096 or offset < header or table_end > total or total > len(blob):
        raise ValueError("invalid DTBO table bounds")
    if not page or page & (page - 1):
        raise ValueError("invalid DTBO page size")
    result, ranges = [], []
    for index in range(count):
        size, start, identity, revision = struct.unpack_from(">4I", blob, offset + stride * index)
        if size < 40 or start < table_end or start + size > total:
            raise ValueError("invalid DTBO entry bounds: " + str(index))
        if any(max(start, a) < min(start + size, b) for a, b in ranges):
            raise ValueError("overlapping DTBO entries")
        entry = blob[start:start + size]
        parse(entry)
        ranges.append((start, start + size))
        result.append({"index": index, "offset": start, "size": size,
                       "id": identity, "revision": revision,
                       "sha256": hashlib.sha256(entry).hexdigest(), "blob": entry})
    return result


def inspect(blob, indices, runtime_root=None):
    entries = table_entries(blob)
    if not indices or len(set(indices)) != len(indices) or any(i < 0 or i >= len(entries) for i in indices):
        raise ValueError("DTBO selection outside table or repeated")
    selected, effective = [], {}
    for index in indices:
        entry = entries[index]
        root = parse(entry["blob"])["/"]
        metadata = {p: root[p] for p in ("model", "compatible", "qcom,board-id", "qcom,msm-id") if p in root}
        effective.update(metadata)
        selected.append({k: v for k, v in entry.items() if k != "blob"} |
                        {"root_metadata_hex": {k: v.hex() for k, v in metadata.items()}})
    result = {"image_sha256": hashlib.sha256(blob).hexdigest(), "entries": len(entries),
              "selected": selected, "scope": "DTBO candidate metadata; not proof of installed firmware"}
    if runtime_root is not None:
        # This is an explanation check, never an exemption in the strict audit.
        properties = ("model", "compatible", "qcom,board-id", "qcom,msm-id")
        compared = [p for p in properties if p in effective and p in runtime_root]
        result["compared_properties"] = compared
        result["metadata_matches_runtime"] = (all(p in compared for p in ("model", "compatible"))
                                                and all(effective[p] == runtime_root[p] for p in compared))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--indices", required=True)
    parser.add_argument("--runtime", type=Path)
    args = parser.parse_args()
    try:
        root = parse(args.runtime.read_bytes())["/"] if args.runtime else None
        report = inspect(args.image.read_bytes(), selected_indices(args.indices), root)
        print(json.dumps(report, indent=2))
        return 1 if report.get("metadata_matches_runtime") is False else 0
    except (OSError, ValueError, struct.error, UnicodeError) as exc:
        parser.exit(1, f"DTBO inspection failed: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
