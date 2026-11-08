"""No-network download failure and content-integrity regressions."""
# SPDX-License-Identifier: AGPL-3.0-only
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from tools import download_locked as fetch

URL = "https://example.invalid/reference.img"
DATA = b"locked metadata"
DIGEST = hashlib.sha256(DATA).hexdigest()


class LockedDownloadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.output = Path(self.tmp.name) / "reference.img"

    def test_504_retry_uses_same_path_and_validates_complete_result(self):
        with patch.object(fetch, "urlopen", side_effect=[HTTPError(URL, 504, "timeout", {}, None), io.BytesIO(DATA)]) as call, patch.object(fetch.time, "sleep"):
            fetch.download(URL, DIGEST, self.output)
        self.assertEqual(self.output.read_bytes(), DATA)
        self.assertEqual(call.call_count, 2)
        self.assertTrue(call.call_args_list[1].args[0].full_url.startswith(URL + "?miseed_retry="))
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_reviewed_api_fallback_still_requires_identical_digest(self):
        api = "https://api.example.invalid/releases/assets/123"
        with patch.object(fetch, "urlopen", side_effect=[HTTPError(URL, 504, "timeout", {}, None), io.BytesIO(DATA)]) as call, patch.object(fetch.time, "sleep"):
            fetch.download(URL, DIGEST, self.output, api)
        self.assertEqual(call.call_args_list[1].args[0].full_url, api)
        self.assertEqual(call.call_args_list[1].args[0].get_header("Accept"), "application/octet-stream")
        self.assertEqual(self.output.read_bytes(), DATA)

    def test_hash_mismatch_is_not_retried_or_cached(self):
        with patch.object(fetch, "urlopen", return_value=io.BytesIO(b"wrong")) as call:
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                fetch.download(URL, DIGEST, self.output)
        self.assertEqual(call.call_count, 1)
        self.assertEqual(list(self.output.parent.iterdir()), [])

    def test_retry_limit_and_404_do_not_leave_partial_cache(self):
        for code, attempts in ((504, 3), (404, 1)):
            with patch.object(fetch, "urlopen", side_effect=HTTPError(URL, code, "failure", {}, None)) as call, patch.object(fetch.time, "sleep"):
                with self.assertRaises(HTTPError):
                    fetch.download(URL, DIGEST, self.output)
            self.assertEqual(call.call_count, attempts)
            self.assertEqual(list(self.output.parent.iterdir()), [])

    def test_existing_cache_is_verified_and_never_overwritten(self):
        self.output.write_bytes(DATA)
        with patch.object(fetch, "urlopen") as call:
            fetch.download(URL, DIGEST, self.output)
            self.output.write_bytes(b"existing wrong input")
            with self.assertRaisesRegex(ValueError, "cache preserved"):
                fetch.download(URL, DIGEST, self.output)
            call.assert_not_called()
        self.assertEqual(self.output.read_bytes(), b"existing wrong input")

    def test_read_failure_removes_partial_download(self):
        class Broken(io.BytesIO):
            def read(self, size=-1):
                if self.tell():
                    raise TimeoutError("partial transfer")
                return super().read(3)
        with patch.object(fetch, "urlopen", side_effect=lambda *a, **kw: Broken(DATA)), patch.object(fetch.time, "sleep"):
            with self.assertRaises(TimeoutError):
                fetch.download(URL, DIGEST, self.output)
        self.assertEqual(list(self.output.parent.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
