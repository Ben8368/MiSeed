"""Audit an exported boot FDT against its exact packaged reference (read-only).

This catches overlay edits; it does not prevent ABL from applying them.
The scope is the full live soc subtree, root identity, aliases, phandles and RAM presence.
It is not a full-device binding or physical-memory safety certification.
"""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import hashlib
import json
import struct
from pathlib import Path

LIMIT = 2 * 1024 * 1024


def cstring(blob, offset, end):
    stop = blob.find(b"\0", offset, end)
    if stop < 0:
        raise ValueError("unterminated FDT string")
    return blob[offset:stop].decode("ascii"), stop + 1


def parse(blob):
    if not 40 <= len(blob) <= LIMIT:
        raise ValueError("FDT size outside audit limit")
    magic, size, off, strings, reserve, version, last, cpu, ns, nt = struct.unpack_from(">10I", blob)
    if magic != 0xd00dfeed or size != len(blob) or version != 17 or last > 17:
        raise ValueError("invalid FDT identity/version/length")
    if off < 40 or strings < 40 or off % 4 or off + nt > size or strings + ns > size:
        raise ValueError("invalid FDT block bounds")
    if max(off, strings) < min(off + nt, strings + ns):
        raise ValueError("overlapping FDT blocks")
    if reserve < 40 or reserve % 8 or reserve + 16 > size:
        raise ValueError("invalid reserve map")
    cursor = reserve
    while True:
        if cursor + 16 > size or max(cursor, off) < min(cursor + 16, off + nt) or max(cursor, strings) < min(cursor + 16, strings + ns):
            raise ValueError("invalid reserve map bounds")
        address, length = struct.unpack_from(">QQ", blob, cursor)
        cursor += 16
        if not address and not length:
            break
    end = off + nt
    stack, nodes = [], {}
    while off + 4 <= end:
        token = struct.unpack_from(">I", blob, off)[0]
        off += 4
        if token == 1:
            name, off = cstring(blob, off, end)
            off = (off + 3) & ~3
            if "/" in name or len(stack) >= 64 or (not stack and name):
                raise ValueError("invalid FDT node name/depth")
            path = "/" + "/".join(stack[1:] + [name]) if stack else "/"
            if path in nodes:
                raise ValueError("duplicate FDT node: " + path)
            stack.append(name)
            nodes[path] = {}
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced END_NODE")
            stack.pop()
        elif token == 3:
            if not stack or off + 8 > end:
                raise ValueError("property outside node")
            length, index = struct.unpack_from(">II", blob, off)
            off += 8
            if index >= ns or off + length > end:
                raise ValueError("property outside FDT block")
            name, _ = cstring(blob, strings + index, strings + ns)
            path = "/" + "/".join(stack[1:])
            if not name or name in nodes[path]:
                raise ValueError("duplicate/empty FDT property")
            nodes[path][name] = blob[off:off + length]
            off = (off + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack or "/" not in nodes:
                raise ValueError("incomplete FDT tree")
            return nodes
        else:
            raise ValueError("invalid FDT token")
    raise ValueError("FDT_END missing")


def string(value):
    if not value or not value.endswith(b"\0") or b"\0" in value[:-1]:
        raise ValueError("expected one NUL-terminated DT string")
    return value[:-1].decode("ascii")


def cells(value):
    if len(value) % 4:
        raise ValueError("invalid DT cells")
    return struct.unpack(">" + "I" * (len(value) // 4), value)


def subtree(tree, path):
    return {p: props for p, props in tree.items() if p == path or p.startswith(path + "/")}


def validate_live(expected, actual):
    for name in ("soc", "spmi_bus", "tlmm", "mdss_mdp", "sdhc_2"):
        prop = "miseed_live_" + name
        live = string(expected.get("/__symbols__", {}).get(prop, b""))
        if not live.startswith("/soc") or actual.get("/__symbols__", {}).get(prop) != expected["/__symbols__"][prop]:
            raise ValueError("live symbol identity changed: " + name)
        sink = string(expected["/__symbols__"][name])
        if not sink.startswith("/abl_ovf/") or actual.get("/__symbols__", {}).get(name) != expected["/__symbols__"][name]:
            raise ValueError("overlay symbol escaped: " + name)
        # These overlays set status=okay themselves; keep an untargeted,
        # disabled parent between their target and the platform bus.
        guard = sink.rsplit("/", 1)[0] if name in ("mdss_mdp", "sdhc_2") else sink
        if guard == "/abl_ovf" and name in ("mdss_mdp", "sdhc_2"):
            raise ValueError("missing nested quarantine: " + name)
        for disabled in ("/abl_ovf", guard):
            if actual.get(disabled, {}).get("status") != b"disabled\0":
                raise ValueError("enabled quarantine: " + disabled)
        if name == "soc":
            children = lambda t: {p for p in t if p.rsplit("/", 1)[0] == live}
            if children(expected) != children(actual) or expected[live] != actual.get(live):
                raise ValueError("live soc topology/properties changed")
        if subtree(expected, live) != subtree(actual, live):
            raise ValueError("protected live subtree changed: " + name)
    if expected.get("/aliases", {}) != actual.get("/aliases", {}):
        raise ValueError("aliases changed; explicit review required")
    handles = {}
    for path, props in actual.items():
        for name in ("phandle", "linux,phandle"):
            if name not in props:
                continue
            values = cells(props[name])
            if len(values) != 1 or values[0] in (0, 0xffffffff):
                raise ValueError("invalid phandle: " + path)
            if values[0] in handles and handles[values[0]] != path:
                raise ValueError("duplicate phandle: " + path)
            if name == "linux,phandle" and "phandle" in props and props[name] != props["phandle"]:
                raise ValueError("conflicting phandle aliases: " + path)
            handles[values[0]] = path


def validate_ram(actual):
    if cells(actual["/"].get("#address-cells", b"")) != (2,) or cells(actual["/"].get("#size-cells", b"")) != (2,):
        raise ValueError("Umi audit requires 64-bit RAM cells")
    ranges = []
    for props in actual.values():
        if props.get("device_type") != b"memory\0":
            continue
        values = cells(props.get("reg", b""))
        if not values or len(values) % 4:
            raise ValueError("invalid RAM reg")
        ranges += [((values[i] << 32) | values[i + 1],
                    (values[i + 2] << 32) | values[i + 3]) for i in range(0, len(values), 4)]
    if not ranges or any(not length or base + length > 1 << 64 for base, length in ranges):
        raise ValueError("runtime RAM range absent/zero/overflow")


def audit_report(reference, runtime):
    expected, actual = parse(reference), parse(runtime)
    issues = []
    for prop in ("compatible", "model", "#address-cells", "#size-cells",
                 "qcom,board-id", "qcom,msm-id", "interrupt-parent"):
        if expected["/"].get(prop) != actual["/"].get(prop):
            issues.append({"section": "root_identity",
                           "message": "root hardware identity changed: " + prop})
    # A root failure must not hide a second, independent hardware-tree failure.
    for section, validate in (("live_hardware", lambda: validate_live(expected, actual)),
                              ("ram", lambda: validate_ram(actual))):
        try:
            validate()
        except ValueError as exc:
            issues.append({"section": section, "message": str(exc)})
    return {"reference_sha256": hashlib.sha256(reference).hexdigest(),
            "runtime_sha256": hashlib.sha256(runtime).hexdigest(),
            "nodes": len(actual), "issues": issues,
            "result": "FAIL" if issues else "PASS scoped boot-FDT audit; L3 bindings separate"}


def audit(reference, runtime):
    report = audit_report(reference, runtime)
    if report["issues"]:
        raise ValueError("; ".join(issue["message"] for issue in report["issues"]))
    return report


def decode_export(text):
    lines = text.splitlines()
    starts = [i for i, line in enumerate(lines) if line.startswith("DTB_BEGIN ")]
    if len(starts) != 1:
        raise ValueError("expected one DTB export")
    start = starts[0]
    size = int(lines[start].split()[1])
    if not 40 <= size <= LIMIT:
        raise ValueError("invalid exported length")
    result = bytearray()
    for line in lines[start + 1:]:
        if line == "DTB_END":
            if len(result) != size:
                raise ValueError("truncated DTB export")
            parse(bytes(result))
            return bytes(result)
        if not line.startswith("DTB "):
            raise ValueError("interrupted DTB export")
        _, offset, data = line.split()
        if int(offset, 16) != len(result):
            raise ValueError("non-contiguous DTB export")
        result.extend(bytes.fromhex(data))
        if len(result) > size:
            raise ValueError("oversized DTB export")
    raise ValueError("DTB_END missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--runtime", type=Path)
    group.add_argument("--serial-export", type=Path)
    args = parser.parse_args()
    try:
        runtime = args.runtime.read_bytes() if args.runtime else decode_export(args.serial_export.read_text())
        report = audit_report(args.reference.read_bytes(), runtime)
        print(json.dumps(report, indent=2))
        if report["issues"]:
            parser.exit(1)
    except (OSError, ValueError, KeyError, struct.error, UnicodeError) as exc:
        parser.exit(1, f"runtime DT audit failed: {exc}\n")


if __name__ == "__main__":
    main()
