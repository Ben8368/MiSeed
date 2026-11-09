#!/usr/bin/env python3
"""Extract only ABL overlay symbol names from a reviewed Umi/CSOT DTB."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ALL_SYMBOL = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^\"]+)";\s*$')
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"|^\s*/include/\s+"([^"]+)"')
ISOLATED_TARGETS = ("soc", "spmi_bus", "tlmm")

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
    if len(sys.argv) not in (6, 7):
        print("usage: extract_abl_anchors.py INPUT.dtb OUTPUT.dtsi BASE.dts INCLUDE_ROOT DTBO_TARGETS.txt [VERIFY.dtb]", file=sys.stderr)
        return 2
    src, dst, base_dts, include_root, target_file = map(Path, sys.argv[1:6])
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
    abl_reference_count = len(names)
    target_names: list[str] = []
    for line_number, raw_line in enumerate(target_file.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", line):
            raise SystemExit(f"invalid DTBO target symbol at {target_file}:{line_number}: {line!r}")
        target_names.append(line)
    if len(target_names) != 241 or len(set(target_names)) != len(target_names):
        raise SystemExit(f"expected 241 unique DTBO target symbols, found {len(target_names)} lines / {len(set(target_names))} unique")
    names = list(dict.fromkeys(names + target_names))
    existing = included_labels(base_dts, include_root)
    missing_names = [name for name in names if name not in existing]
    content = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "/* Empty ABL overlay targets; names extracted from the locked CSOT reference DTB and",
        " * the local UMI DTBO reference symbol list. No overlay properties or boot payloads included. */",
        "abl_ovf {",
        '\t\tstatus = "disabled";',
    ]
    content.extend(f"\t\t{name}: ovf{i} {{ }};" for i, name in enumerate(missing_names))
    # These downstream targets must never resolve to live mainline buses.
    # Export is redirected after dtc; compiled mainline phandles stay intact.
    for name in ISOLATED_TARGETS:
        content.append(f'\t\tmiseed_dtbo_{name}: isolated-{name.replace("_", "-")} {{ status = "disabled"; }};')
    content.append("\t};")
    generated = "\n".join(content) + "\n"
    if len(sys.argv) == 7:
        if not dst.is_file() or dst.read_text(encoding="utf-8") != generated:
            raise SystemExit("generated ABL anchor include differs from the locked DTS inputs")
        built_symbols = symbols(Path(sys.argv[6]))
        unresolved = sorted(set(names) - set(built_symbols))
        if unresolved:
            raise SystemExit(f"DTB is missing {len(unresolved)} reference overlay symbols: {', '.join(unresolved[:8])}")
        print(f"verified {abl_reference_count} ABL reference anchors and all {len(target_names)} DTBO target symbols in the built DTB")
    else:
        dst.write_text(generated, encoding="utf-8", newline="\n")
        print(f"wrote {len(missing_names)} empty overlay anchors for {abl_reference_count} ABL reference anchors and {len(target_names)} DTBO targets; {len(names) - len(missing_names)} symbols already exist in the Linux DTS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
