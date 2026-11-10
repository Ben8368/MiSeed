"""Source size gates: physical boundaries, discovery, and CLI exit status."""

# SPDX-License-Identifier: AGPL-3.0-only

import contextlib
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import check_repo


class SourceLimitTests(unittest.TestCase):
    def test_boundaries_and_final_newline(self):
        for lines, level in [(0, None), (300, None), (301, "YELLOW"),
                             (600, "YELLOW"), (601, "RED")]:
            for final_newline in (True, False):
                with self.subTest(lines=lines, final_newline=final_newline):
                    data = b"/* comment */\n" * lines
                    if not final_newline:
                        data = data.removesuffix(b"\n")
                    errors, warnings = check_repo.source_diagnostics("driver.c", data)
                    self.assertEqual(bool(errors), level == "RED")
                    self.assertEqual(bool(warnings), level == "YELLOW")
                    if level:
                        self.assertIn(f"{lines} source lines", (errors + warnings)[0])

    def test_blank_lines_and_crlf_are_counted(self):
        for data in (b"\n" * 601, b"// comment\r\n" * 601):
            errors, warnings = check_repo.source_diagnostics("local.h", data)
            self.assertIn("601 source lines", errors[0])
            self.assertEqual(warnings, [])

    def test_source_scope_includes_imports_tests_and_build_inputs(self):
        for name in ("kernel/power/qcom_fg.c", "tests/harness.inc", "module.h",
                     "main.cpp", "shared.hpp", "boot.S", "boot.asm", "lib.rs",
                     "tools/check.py", "build.sh", "host.ps1", "board.dts",
                     "board.dtsi", "Kconfig", "Kconfig.board", "Kbuild",
                     "Makefile", "Makefile.board", "board.mk", "Dockerfile",
                     "Dockerfile.build", "tools/macos-l2.Dockerfile"):
            with self.subTest(name=name):
                self.assertTrue(check_repo.source_diagnostics(name, b"\n" * 601)[0])

    def test_non_source_material_has_no_source_limit(self):
        for name in ("LICENSE", "LICENSES/GPL-2.0-only.txt", "docs/QUALITY.md",
                     "manifests/sources.lock.json", "binding.yaml", "ci.yml",
                     "kernel/board.config", "docs/archive/raw.log"):
            with self.subTest(name=name):
                self.assertEqual(check_repo.source_diagnostics(name, b"\n" * 601),
                                 ([], []))

    def test_git_discovery_cannot_hide_tracked_or_new_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / ".gitignore").write_bytes(b"ignored.c\n.work/\n")
            tracked = root / "ignored.c"
            tracked.write_bytes(b"\n" * 601)
            subprocess.run(["git", "-C", str(root), "add", "-f", "ignored.c"],
                           check=True)
            added = root / "new.inc"
            added.write_bytes(b"\n" * 301)
            (root / ".work").mkdir()
            (root / ".work/linux.c").write_bytes(b"\n" * 601)
            warnings = []
            errors = check_repo.check_repository(root, warnings)
            self.assertTrue(any("ignored.c: RED" in error for error in errors))
            self.assertTrue(any("new.inc: YELLOW" in item for item in warnings))
            self.assertFalse(any("linux.c" in item for item in errors + warnings))

    def test_no_git_discovery_and_debt_cannot_exempt_red(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "local.c").write_bytes(b"\n" * 601)
            (root / "docs").mkdir()
            (root / "docs/TECH_DEBT.md").write_bytes(b"`local.c`: accepted debt\n")
            (root / "out").mkdir()
            (root / "out/generated.c").write_bytes(b"\n" * 601)
            warnings = []
            errors = check_repo.check_repository(root, warnings)
            self.assertTrue(any("local.c: RED" in error for error in errors))
            self.assertFalse(any("generated.c" in item for item in errors + warnings))

    def test_cli_red_fails_and_yellow_stays_visible_on_success(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "fixture.c"
            (root / "docs").mkdir()
            (root / "docs/CONTEXT.md").write_text(
                "## 近期优先级\n1. Fixture\n", encoding="utf-8", newline="\n")
            for lines, status, marker in [(300, 0, "PASS:"),
                                           (301, 0, "WARNING:"),
                                           (601, 1, "ERROR:")]:
                source.write_bytes(b"\n" * lines)
                output = io.StringIO()
                with (patch.object(check_repo, "REQUIRED", ()),
                      patch.object(check_repo, "check_build_lock", return_value=[]),
                      patch.object(check_repo, "adr_errors", return_value=[]),
                      patch("sys.argv", ["check_repo.py", "--root", str(root)]),
                      contextlib.redirect_stdout(output)):
                    self.assertEqual(check_repo.main(), status)
                self.assertIn(marker, output.getvalue())
                self.assertEqual("PASS:" in output.getvalue(), status == 0)


if __name__ == "__main__":
    unittest.main()
