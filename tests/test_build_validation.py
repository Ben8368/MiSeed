"""Build-warning and mandatory DT validation failure boundaries."""

# SPDX-License-Identifier: AGPL-3.0-only

import io
import contextlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from tools import run_build_command, validate_kernel_dt


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "POSIX bash required")
class NativeOutputTests(unittest.TestCase):
    def test_native_output_preflight_rejects_default_aliases(self):
        root = Path(__file__).resolve().parents[1]
        script = (root / "tools/build_kernel_l2.sh").read_text(encoding="utf-8")
        # Execute the actual script preflight without build/download operations.
        preflight = script.split("# Capture recursive make diagnostics too:", 1)[0]
        preflight = preflight.replace(
            'readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"',
            'readonly repo_root="$MISEED_TEST_REPO_ROOT"')
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            default = repo / ".work/boot-l2"
            default.mkdir(parents=True)
            alias = repo / "default-alias"
            alias.symlink_to(default, target_is_directory=True)
            for output in (None, str(default), ".work/boot-l2",
                           str(default) + "/../boot-l2/.", str(alias), str(repo / "native")):
                env = dict(os.environ, MISEED_NATIVE_DSI="1", MISEED_TEST_REPO_ROOT=str(repo))
                env.pop("MISEED_OUTPUT_ROOT", None)
                if output is not None:
                    env["MISEED_OUTPUT_ROOT"] = output
                with self.subTest(output=output):
                    result = subprocess.run(["bash", "-c", preflight], cwd=repo, env=env,
                                            capture_output=True, text=True)
                    if output == str(repo / "native"):
                        self.assertEqual(result.returncode, 0, result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("distinct explicit MISEED_OUTPUT_ROOT", result.stderr)
            self.assertFalse((repo / "native").exists())


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "POSIX bash required")
class CiAptTests(unittest.TestCase):
    def test_actual_install_uses_isolated_signed_sources_and_retains_pins(self):
        self.run_install(False)

    def test_failed_index_refresh_stops_before_package_install(self):
        self.run_install(True)

    def run_install(self, fail_update):
        root = Path(__file__).resolve().parents[1]
        workflow = (root / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        step = workflow.split("      - name: Install pinned kernel build tools\n", 1)[1]
        step = step.split("      - name:", 1)[0]
        script = "\n".join(line[10:] for line in step.split("        run: |\n", 1)[1].splitlines())
        with tempfile.TemporaryDirectory(prefix="apt fixture ") as directory:
            tmp = Path(directory)
            binary = tmp / "bin"
            binary.mkdir()
            sudo = binary / "sudo"
            sudo.write_text('''#!/usr/bin/env bash
printf '%s\\0' "$@" >> "$TEST_APT_CALLS"
printf '\\n' >> "$TEST_APT_CALLS"
for argument in "$@"; do
  if [[ "$argument" == update && "$TEST_APT_FAIL_UPDATE" == 1 ]]; then
    exit 100
  fi
done
''', encoding="utf-8")
            sudo.chmod(0o755)
            calls_file = tmp / "calls"
            env = dict(os.environ, RUNNER_TEMP=str(tmp / "runner temp"),
                       PATH=str(binary) + os.pathsep + os.environ["PATH"],
                       TEST_APT_CALLS=str(calls_file),
                       TEST_APT_FAIL_UPDATE=str(int(fail_update)))
            result = subprocess.run(["bash", "-eu", "-o", "pipefail", "-c", script],
                                    cwd=tmp, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 100 if fail_update else 0, result.stderr)
            calls = [line.decode().split("\0")[:-1]
                     for line in calls_file.read_bytes().splitlines()]
            self.assertEqual(len(calls), 1 if fail_update else 2)
            for call in calls:
                self.assertEqual(call[0], "apt-get")
                for option in ("Acquire::Retries=3", "Acquire::http::Timeout=20",
                               "Acquire::https::Timeout=20", "APT::Update::Error-Mode=any"):
                    self.assertIn(option, call)
                self.assertIn("Dir::Etc::sourceparts=" + str(tmp / "runner temp/miseed-apt/empty-parts"), call)
            sources = tmp / "runner temp/miseed-apt/sources.list"
            self.assertTrue((sources.parent / "empty-parts").is_dir())
            text = sources.read_text()
            self.assertEqual(text.count("signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg"), 3)
            self.assertIn("https://archive.ubuntu.com/ubuntu noble-updates", text)
            self.assertIn("https://security.ubuntu.com/ubuntu noble-security", text)
            self.assertNotIn("mirror+file", text)
            self.assertNotIn("trusted=yes", text)
            if not fail_update:
                self.assertIn("gcc-13-aarch64-linux-gnu=13.3.0-6ubuntu2~24.04.1cross1", calls[1])
                self.assertIn("libssl-dev=3.0.13-0ubuntu3.16", calls[1])
                self.assertNotIn("--allow-unauthenticated", calls[1])


class BuildCommandTests(unittest.TestCase):
    def invoke(self, body):
        stream = io.BytesIO()
        code = run_build_command.run([sys.executable, "-c", body], stream)
        return code, stream.getvalue()

    def test_normal_warning_and_failure_exit_are_preserved(self):
        code, log = self.invoke("print('ordinary warning')")
        self.assertEqual(code, 0)
        self.assertIn(b"ordinary warning", log)
        self.assertEqual(self.invoke("raise SystemExit(7)")[0], 7)

    def test_clock_warning_cannot_be_reported_as_success(self):
        for text in ("make: warning: Clock skew detected. Your build may be incomplete.",
                     "make: Warning: File 'auto.conf' has modification time 2.6 s in the future"):
            with self.subTest(text=text):
                code, log = self.invoke("import sys; print(" + repr(text) + ", file=sys.stderr)")
                self.assertEqual(code, 1)
                self.assertIn(text.encode(), log)
                self.assertIn(b"Timestamps were not changed", log)


class DtValidationTests(unittest.TestCase):
    def test_zero_exit_with_diagnostics_is_rejected(self):
        for stdout, stderr in (("schema failure\n", ""), ("", "schema failure\n")):
            with patch.object(subprocess, "run", return_value=subprocess.CompletedProcess(
                    ["dt-validate"], 0, stdout, stderr)), contextlib.redirect_stdout(
                        io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(ValueError, "diagnostics"):
                    validate_kernel_dt.checked(["dt-validate"], {}, silent=True)

    def test_missing_or_wrong_dtschema_cannot_skip(self):
        for version in ("0.0", None):
            with patch.object(validate_kernel_dt.importlib.metadata, "version") as get_version:
                if version is None:
                    get_version.side_effect = validate_kernel_dt.importlib.metadata.PackageNotFoundError
                else:
                    get_version.return_value = version
                with self.assertRaises((ValueError,
                                        validate_kernel_dt.importlib.metadata.PackageNotFoundError)):
                    validate_kernel_dt.validate(None, None, None)

    def test_specialized_suite_rejects_skips_and_missing_tests(self):
        for count, skipped in ((4, [("test", "missing tool")]), (0, [])):
            result = SimpleNamespace(testsRun=count, skipped=skipped, wasSuccessful=lambda: True)
            with patch.dict(validate_kernel_dt.os.environ), patch.object(
                    validate_kernel_dt.unittest.defaultTestLoader, "discover"), patch.object(
                    validate_kernel_dt.unittest, "TextTestRunner") as runner:
                runner.return_value.run.return_value = result
                with self.assertRaisesRegex(ValueError, "failed, skipped, or changed"):
                    validate_kernel_dt.run_regressions("dtb", "schema", {})

    def test_yamllint_is_required_before_kbuild(self):
        for version, executable in ((None, "/fixture"), ("0.0", "/fixture"),
                                    (validate_kernel_dt.YAMLLINT_VERSION, None)):
            def metadata(name):
                if name == "dtschema":
                    return validate_kernel_dt.DTSCHEMA_VERSION
                if version is None:
                    raise validate_kernel_dt.importlib.metadata.PackageNotFoundError(name)
                return version
            with self.subTest(version=version, executable=executable), patch.object(
                    validate_kernel_dt.importlib.metadata, "version", side_effect=metadata), patch.object(
                    validate_kernel_dt.shutil, "which", side_effect=lambda name, **kw:
                    executable if name == "yamllint" else "/fixture"):
                with self.assertRaises((ValueError,
                                        validate_kernel_dt.importlib.metadata.PackageNotFoundError)):
                    validate_kernel_dt.validate(None, None, None)


if __name__ == "__main__":
    unittest.main()
