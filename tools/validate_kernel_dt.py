"""Validate staged bindings and the final Umi DTB, then run DT regressions."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import importlib.metadata
import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

if __package__:
    from .apply_power_port import REVISION
    from .check_wired_dt import SCHEMAS
else:
    from apply_power_port import REVISION
    from check_wired_dt import SCHEMAS


ROOT = Path(__file__).resolve().parents[1]
DTSCHEMA_VERSION = "2025.8"
SCOPE = SCHEMAS + ("net/nfc/nxp,sn100t.yaml", "display/simple-framebuffer.yaml")


def checked(command, env, *, silent=False):
    result = subprocess.run(command, env=env, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode or result.stderr.strip() or (silent and result.stdout.strip()):
        raise ValueError("DT validation command emitted diagnostics or failed: " + str(command))


def run_regressions(dtb, schema, env):
    # A specialized entry point must never count skipped tests as success.
    os.environ.update(env, MISEED_WIRED_DTB=str(dtb), MISEED_DT_SCHEMA=str(schema))
    sys.path.insert(0, str(ROOT))
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tests"), "test_wired_dt.py")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful() or result.skipped or result.testsRun != 4:
        raise ValueError("DT regressions failed, skipped, or changed; review the required suite")


def validate(source, build, dtb):
    version = importlib.metadata.version("dtschema")
    if version != DTSCHEMA_VERSION:
        raise ValueError(f"dtschema {DTSCHEMA_VERSION} required; found {version}")
    env = dict(os.environ, LC_ALL="C")
    env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env.get("PATH", "")
    for tool in ("make", "dt-doc-validate", "dt-mk-schema", "dt-validate",
                 "dt-extract-example", "fdtget", "fdtput", "aarch64-linux-gnu-gcc"):
        if not shutil.which(tool, path=env["PATH"]):
            raise ValueError("missing DT validation tool: " + tool)
    if (source / ".miseed-source-revision").read_text().strip() != REVISION:
        raise ValueError("DT source revision mismatch; run the locked L2 build first")
    for entry in (build / ".config", dtb):
        if not entry.is_file():
            raise ValueError("missing L2 input: " + str(entry))
    bindings = source / "Documentation/devicetree/bindings"
    checked(["dt-doc-validate", *[str(bindings / name) for name in SCOPE]], env, silent=True)
    command = ["make", "-C", str(source), "O=" + str(build), "ARCH=arm64",
               "CROSS_COMPILE=aarch64-linux-gnu-", "DT_SCHEMA_FILES=" + ":".join(SCOPE)]
    checked(command + ["dt_binding_check"], env)
    # Invalidate only the generated board DTB. make -B also remakes Kconfig's
    # included files and is unsuitable for a prepared kernel build tree.
    board = build / "arch/arm64/boot/dts/qcom/umi-csot.dtb"
    if dtb == board:
        raise ValueError("--dtb must be the final packaged copy, separate from Kbuild output")
    board.unlink(missing_ok=True)
    checked(command + ["CHECK_DTBS=y", "qcom/umi-csot.dtb"], env)
    schema = build / "Documentation/devicetree/bindings/processed-schema.json"
    indexed = json.loads(schema.read_text())
    for name in SCOPE:
        if "http://devicetree.org/schemas/" + name + "#" not in indexed:
            raise ValueError("schema was not loaded: " + name)
    # make/dt-validate can exit zero while printing schema failures.
    checked(["dt-validate", "-s", str(schema), "-l", ":".join(SCOPE), str(dtb)],
            env, silent=True)
    run_regressions(dtb, schema, env)
    print("PASS: scoped binding, board/final DTB and four DT regressions; no skips")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "build", "dtb"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        validate(args.source.resolve(), args.build.resolve(), args.dtb.resolve())
    except (OSError, ValueError, importlib.metadata.PackageNotFoundError) as exc:
        parser.exit(1, f"DT validation failed: {exc}\n")


if __name__ == "__main__":
    main()
