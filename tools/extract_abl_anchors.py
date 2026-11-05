#!/usr/bin/env python3
"""Extract only ABL overlay symbol names from a reviewed Umi/CSOT DTB."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ALL_SYMBOL = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^\"]+)";\s*$')
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"|^\s*/include/\s+"([^"]+)"')
LABEL = re.compile(r'(?m)^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:')


def symbols(path: Path) -> dict[str, str]:
    text = subprocess.run(
        ["dtc", "-I", "dtb", "-O", "dts", str(path)],
        check=True, capture_output=True, text=True,
    ).stdout
    result: dict[str, str] = {}
    in_symbols = False
    for line in text.splitlines():
        if re.match(r"\s*__symbols__\s*\{", line):
            in_symbols = True
            continue
        if in_symbols and re.match(r"\s*};", line):
            break
        if in_symbols:
            match = ALL_SYMBOL.match(line)
            if match:
                result[match.group(1)] = match.group(2)
    return result


def included_labels(entry: Path, include_root: Path) -> set[str]:
    visited: set[Path] = set()
    labels: set[str] = set()

    def visit(path: Path) -> None:
        path = path.resolve()
        if path in visited or not path.is_file():
            return
        visited.add(path)
        text = path.read_text(encoding="utf-8")
        labels.update(LABEL.findall(text))
        for line in text.splitlines():
            match = INCLUDE.match(line)
            if not match:
                continue
            name = match.group(1) or match.group(2)
            if Path(name).name == "umi-csot-abl-anchors.dtsi":
                continue
            options = (path.parent / name, include_root / name)
            child = next((option for option in options if option.is_file()), None)
            if child:
                visit(child)

    visit(entry)
    return labels


def main() -> int:
    if len(sys.argv) not in (5, 6):
        print("usage: extract_abl_anchors.py INPUT.dtb OUTPUT.dtsi BASE.dts INCLUDE_ROOT [VERIFY.dtb]", file=sys.stderr)
        return 2
    src, dst, base_dts, include_root = map(Path, sys.argv[1:5])
    ref_symbols = symbols(src)
    names = []
    for name, path in ref_symbols.items():
        if path.startswith("/abl_ovf/"):
            if path.count("/") != 2:
                raise SystemExit(f"unexpected ABL anchor path: {path}")
            names.append(name)
    names = list(dict.fromkeys(names))
    if len(names) < 152:
        raise SystemExit(f"expected at least 152 ABL anchors, found {len(names)}")
    existing = included_labels(base_dts, include_root)
    missing_names = [name for name in names if name not in existing]
    content = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "/* Empty ABL overlay targets; names extracted from the locked CSOT reference DTB.",
        " * No upstream properties or boot payload are included. */",
        "abl_ovf {",
    ]
    content.extend(f"\t\t{name}: ovf{i} {{ }};" for i, name in enumerate(missing_names))
    content.append("\t};")
    generated = "\n".join(content) + "\n"
    if len(sys.argv) == 6:
        if not dst.is_file() or dst.read_text(encoding="utf-8") != generated:
            raise SystemExit("generated ABL anchor include differs from the locked DTS inputs")
        built_symbols = symbols(Path(sys.argv[5]))
        unresolved = sorted(set(names) - set(built_symbols))
        if unresolved:
            raise SystemExit(f"DTB is missing {len(unresolved)} reference overlay symbols: {', '.join(unresolved[:8])}")
        print(f"verified all {len(names)} locked ABL overlay symbols in the built DTB")
    else:
        dst.write_text(generated, encoding="utf-8", newline="\n")
        print(f"wrote {len(missing_names)} empty ABL overlay anchors; {len(names) - len(missing_names)} symbols already exist in the Linux DTS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
