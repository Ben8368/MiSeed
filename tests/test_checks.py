"""Failure-oriented offline regressions; all Git mutations use temporary repos."""

# SPDX-License-Identifier: AGPL-3.0-only

import copy
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import check_commits, check_repo


ROOT = Path(__file__).resolve().parents[1]


class RepositoryChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        shutil.copytree(ROOT, self.root,
                        ignore=shutil.ignore_patterns(*check_repo.EXCLUDED))

    def errors(self):
        return check_repo.check_repository(self.root)

    def test_valid_repository(self):
        self.assertEqual(self.errors(), [])

    def test_bad_encoding_and_line_endings(self):
        for data, fragment in [(b"\xff", "UTF-8"), (b"line\r\n", "LF"),
                               (b"line \n", "whitespace"), (b"line", "newline"),
                               (b"\xef\xbb\xbftext\n", "BOM")]:
            with self.subTest(fragment=fragment):
                errors, _ = check_repo.text_errors("file.md", data)
                self.assertTrue(any(fragment in item for item in errors))

    def test_budget_overflow(self):
        path = self.root / "AGENTS.md"
        path.write_text("line\n" * 46, encoding="utf-8", newline="\n")
        self.assertTrue(any("budget" in error for error in self.errors()))

    def test_active_document_and_adr_budget(self):
        for name, data in [("docs/extra.md", b"line\n" * 61),
                           ("docs/extra.md", ("字" * 3500 + "\n").encode("utf-8")),
                           ("docs/ADR/0004-extra.md", b"line\n" * 31),
                           ("README.md", b"line\n" * 46)]:
            with self.subTest(name=name, size=len(data)):
                errors, _ = check_repo.text_errors(name, data)
                self.assertTrue(any("budget" in error for error in errors))

    def test_archive_budget_exemption_keeps_text_validation(self):
        errors, _ = check_repo.text_errors("docs/archive/history.md", b"line\n" * 100)
        self.assertEqual(errors, [])
        errors, _ = check_repo.text_errors("docs/archive/history.md", b"line\r\n" * 100)
        self.assertTrue(any("LF" in error for error in errors))

    def test_missing_file(self):
        (self.root / "docs/PRODUCT.md").unlink()
        self.assertTrue(any("missing required" in error for error in self.errors()))

    def test_broken_and_escaping_links(self):
        content = "[missing](absent.md)\n[escape](../../outside.md)\n"
        errors = check_repo.markdown_errors(self.root, self.root / "README.md", content)
        self.assertEqual(len(errors), 2)

    def test_links_ignore_examples_and_accept_encoded_names(self):
        (self.root / "a b.md").write_text("ok\n", encoding="utf-8")
        content = ("[ok](a%20b.md)\n[web](https://example.com)\n"
                   "```text\n[example](absent.md)\n```\n")
        self.assertEqual(check_repo.markdown_errors(
            self.root, self.root / "README.md", content), [])

    def test_links_use_canonical_paths_and_keep_escape_checks(self):
        (self.root / "nested").mkdir()
        (self.root / "a b.md").write_text("ok\n", encoding="utf-8")
        (self.root.parent / "outside.md").write_text("outside\n", encoding="utf-8")
        alias = self.root / "nested" / ".."
        self.assertEqual(check_repo.markdown_errors(
            alias, alias / "README.md", "[ok](a%20b.md)\n"), [])
        errors = check_repo.markdown_errors(
            alias, alias / "README.md", "[escape](../outside.md)\n")
        self.assertEqual(len(errors), 1)
        self.assertIn("broken/escaping link", errors[0])

    def test_adr_index_mismatch(self):
        path = self.root / "docs/ADR/0002-independent-integration.md"
        path.write_text(path.read_text(encoding="utf-8").replace(
            "状态：已接受", "状态：设计中"), encoding="utf-8", newline="\n")
        self.assertTrue(any("index/status" in error for error in self.errors()))

    def test_extra_priorities(self):
        path = self.root / "docs/CONTEXT.md"
        content = path.read_text(encoding="utf-8")
        marker = "## 近期优先级\n"
        self.assertIn(marker, content, "priority fixture heading changed")
        path.write_text(content.replace(
            marker, marker + "4. 额外任务\n", 1),
            encoding="utf-8", newline="\n")
        self.assertTrue(any("priorities" in error for error in self.errors()))

    def test_artifacts_and_sensitive_inputs(self):
        for filename in ["boot.img", "private.key", ".env", "raw.log"]:
            (self.root / filename).write_bytes(b"fixture\n")
        self.assertEqual(sum("forbidden artifact" in error for error in self.errors()), 4)

    def test_invalid_json(self):
        (self.root / "manifests/sources.json").write_text("{\n", encoding="utf-8")
        self.assertTrue(any("invalid JSON" in error for error in self.errors()))

    def test_git_tracked_ignored_artifact_is_not_hidden(self):
        check_commits.git(self.root, "init", "-q")
        (self.root / "boot.img").write_bytes(b"fixture\n")
        (self.root / "out").mkdir()
        (self.root / "out/metadata.json").write_text("{}\n", encoding="utf-8")
        check_commits.git(self.root, "add", "-f", "boot.img", "out/metadata.json")
        errors = self.errors()
        self.assertTrue(any("boot.img: forbidden artifact" in error for error in errors))
        self.assertTrue(any("tracked local/generated" in error for error in errors))

    def test_candidate_cannot_be_used_as_lock(self):
        data = json.loads((ROOT / "manifests/sources.json").read_text(encoding="utf-8"))
        for key, value in [("revision", "a" * 40), ("license_review", "approved"),
                           ("status", "imported"), ("url", "http://example.com")]:
            with self.subTest(key=key):
                changed = copy.deepcopy(data)
                changed["candidates"][0][key] = value
                self.assertTrue(check_repo.sources_errors(changed))
        data["candidates"].append(copy.deepcopy(data["candidates"][0]))
        self.assertTrue(any("duplicate" in e for e in check_repo.sources_errors(data)))

    def test_malformed_candidates_fail_cleanly(self):
        for value in [None, [], {}, {"schema_version": True, "candidates": []},
                      {"schema_version": 1, "candidates": [None]}]:
            with self.subTest(value=value):
                self.assertTrue(check_repo.sources_errors(value))


class CommitChecks(unittest.TestCase):
    def test_authorship_classification(self):
        title = "docs: 初始化项目文档\n"
        for trailer in ["Co-authored-by: Codex <codex@openai.com>\n",
                        "Human-authored: true\n", "Bot-authored-by: Test Bot\n"]:
            with self.subTest(trailer=trailer):
                self.assertEqual(check_commits.message_errors(title, trailer), [])

    def test_missing_false_and_conflicting_attribution(self):
        title = "ci: 添加检查\n"
        for trailer in ["", "Human-authored: false\n",
                        "Human-authored: true\nHuman-authored: true\n",
                        "Human-authored: true\nCo-authored-by: Codex <codex@openai.com>\n",
                        "Human-authored: true\nBot-authored-by: Test\n"]:
            with self.subTest(trailer=trailer):
                self.assertTrue(check_commits.message_errors(title, trailer))

    def test_chinese_conventional_title(self):
        for title in ["Update docs", "docs: English only", "初始化文档", ""]:
            self.assertTrue(check_commits.message_errors(title, "Human-authored: true\n"))

    def test_real_git_range_and_trailer_parsing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.git(root, "init", "-q")
            self.git(root, "config", "user.name", "Offline Test Fixture")
            self.git(root, "config", "user.email", "fixture@example.invalid")
            self.git(root, "config", "commit.gpgsign", "false")
            # Historical nonconforming commits are deliberately outside the range.
            base = self.commit(root, "legacy fixture")
            head = self.commit(root, "docs: 合法夹具\n\nHuman-authored: true")
            commits, errors = check_commits.check_range(root, base, head)
            self.assertEqual(commits, [head])
            self.assertEqual(errors, [])
            bad = self.commit(root, "docs: 缺失署名夹具")
            self.assertTrue(check_commits.check_range(root, head, bad)[1])
            # Attribution mentioned in prose is not an actual terminal trailer.
            fake = self.commit(root, "docs: 正文署名夹具\n\nHuman-authored: true\n\n正文说明")
            self.assertTrue(check_commits.check_range(root, bad, fake)[1])
            with self.assertRaises(ValueError):
                check_commits.check_range(root, "--all", head)
            with self.assertRaises(subprocess.CalledProcessError):
                check_commits.check_range(root, "a" * 40, head)

    @staticmethod
    def git(root, *args):
        return check_commits.git(root, *args)

    def commit(self, root, message):
        self.git(root, "-c", "core.hooksPath=", "commit", "--allow-empty", "-q", "-m", message)
        return self.git(root, "rev-parse", "HEAD").strip()


if __name__ == "__main__":
    unittest.main()
