"""Offline regressions for locked Linux source mirror fallback."""

# SPDX-License-Identifier: AGPL-3.0-only

import subprocess
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from tools import fetch_kernel_source
from tools.fetch_kernel_source import ensure_locked_source


class KernelSourceFetchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.mirror = self.root / "mirror.git"
        self.primary = self.root / "primary.git"
        self.cache = self.root / "cache.git"
        self.source.mkdir()
        self.call(self.source, "init", "-q")
        self.call(self.source, "-c", "user.name=Fixture", "-c",
                  "user.email=fixture@example.invalid", "commit", "--allow-empty", "-qm", "fixture")
        self.revision = self.call(self.source, "rev-parse", "HEAD")
        self.call(self.source, "-c", "user.name=Fixture", "-c",
                  "user.email=fixture@example.invalid", "tag", "-a", "v1.2.3", "-m", "fixture")
        self.tag_object = self.call(self.source, "rev-parse", "refs/tags/v1.2.3")
        self.call(None, "clone", "-q", "--bare", str(self.source), str(self.mirror))
        self.call(None, "init", "-q", "--bare", str(self.primary))
        self.call(None, "init", "-q", "--bare", str(self.cache))
        self.call(self.cache, "remote", "add", "origin", self.primary.as_uri())

    @staticmethod
    def call(repository, *args):
        command = ["git"]
        if repository is not None:
            command.extend(["-C", str(repository)])
        command.extend(args)
        return subprocess.run(command, check=True, capture_output=True, text=True,
                              encoding="utf-8").stdout.strip()

    def test_primary_failure_fetches_exact_tag_from_mirror(self):
        ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                             "v1.2.3", self.tag_object, self.revision)
        self.assertEqual(self.call(self.cache, "rev-parse", f"{self.revision}^{{commit}}"), self.revision)
        self.assertEqual(self.call(self.cache, "remote", "get-url", "origin"), self.mirror.as_uri())
        self.assertEqual(self.call(self.cache, "rev-parse", "refs/miseed/v1.2.3"), self.tag_object)

    def test_mirror_with_different_tag_object_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "tag object mismatch"):
            ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                 "v1.2.3", "0" * 40, self.revision)

    def test_new_cache_is_initialized_and_fetches_locked_mirror(self):
        fresh = self.root / "fresh.git"
        ensure_locked_source(fresh, self.primary.as_uri(), self.mirror.as_uri(),
                             "v1.2.3", self.tag_object, self.revision)
        self.assertEqual(self.call(fresh, "rev-parse", "refs/miseed/v1.2.3"), self.tag_object)

    def test_primary_success_keeps_primary_origin(self):
        self.call(self.primary, "fetch", self.mirror.as_uri(), self.revision)
        ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                             "v1.2.3", self.tag_object, self.revision)
        self.assertEqual(self.call(self.cache, "remote", "get-url", "origin"), self.primary.as_uri())

    def test_cache_hit_needs_no_remote(self):
        self.call(self.cache, "fetch", self.mirror.as_uri(), self.revision)
        missing = (self.root / "missing.git").as_uri()
        self.call(self.cache, "remote", "set-url", "origin", missing)
        ensure_locked_source(self.cache, missing, self.mirror.as_uri(),
                             "v1.2.3", self.tag_object, self.revision)

    def test_mirror_commit_mismatch_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "mirror commit mismatch"):
            ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                 "v1.2.3", self.tag_object, "1" * 40)

    def test_existing_mirror_origin_still_checks_tag(self):
        self.call(self.cache, "remote", "set-url", "origin", self.mirror.as_uri())
        with self.assertRaisesRegex(ValueError, "tag object mismatch"):
            ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                 "v1.2.3", "0" * 40, self.revision)

    def test_mirror_fetch_failure_propagates(self):
        missing = (self.root / "missing.git").as_uri()
        with self.assertRaises(subprocess.CalledProcessError):
            ensure_locked_source(self.cache, self.primary.as_uri(), missing,
                                 "v1.2.3", self.tag_object, self.revision)

    def test_invalid_lock_is_rejected_before_fetch(self):
        for tag, tag_object in (("--upload-pack=bad", self.tag_object), ("v1.2.3", "bad")):
            with self.subTest(tag=tag, tag_object=tag_object), self.assertRaises(ValueError):
                ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                     tag, tag_object, self.revision)

    def test_cached_mirror_cannot_bypass_failed_tag_validation(self):
        for _ in range(2):
            with self.assertRaisesRegex(ValueError, "tag object mismatch"):
                ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                     "v1.2.3", "0" * 40, self.revision)

    def test_primary_timeout_falls_back(self):
        original = fetch_kernel_source.git

        def run(cache, *args, **kwargs):
            if args == ("fetch", "--depth=1", "--filter=blob:none", "origin", self.revision):
                self.assertEqual(kwargs["timeout"], 60)
                raise subprocess.TimeoutExpired("git fetch", 60)
            return original(cache, *args, **kwargs)

        with patch.object(fetch_kernel_source, "git", side_effect=run):
            ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                 "v1.2.3", self.tag_object, self.revision)
        self.assertEqual(self.call(self.cache, "remote", "get-url", "origin"), self.mirror.as_uri())

    def test_unreviewed_existing_origin_is_rejected(self):
        self.call(self.cache, "remote", "set-url", "origin", "https://example.invalid/linux.git")
        with self.assertRaisesRegex(ValueError, "unreviewed origin"):
            ensure_locked_source(self.cache, self.primary.as_uri(), self.mirror.as_uri(),
                                 "v1.2.3", self.tag_object, self.revision)


if __name__ == "__main__":
    unittest.main()
