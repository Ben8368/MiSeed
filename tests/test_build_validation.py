"""Build-warning and mandatory DT validation failure boundaries."""

# SPDX-License-Identifier: AGPL-3.0-only

import io
import contextlib
import subprocess
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from tools import run_build_command, validate_kernel_dt


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
