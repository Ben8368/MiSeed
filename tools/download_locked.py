"""Fetch a hash-locked build input, retrying only transient transport failures."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import hashlib
from http.client import IncompleteRead
import os
from pathlib import Path
import re
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit, urlunsplit
from urllib.request import Request, urlopen


def download(url, digest, output, fallback_url=None):
    output = Path(output)
    if any(urlsplit(u).scheme != "https" for u in (url, fallback_url or url)) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("expected HTTPS source and full SHA-256")
    if output.exists():
        if hashlib.sha256(output.read_bytes()).hexdigest() != digest:
            raise ValueError("existing reference SHA-256 mismatch; cache preserved")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        parts = urlsplit(fallback_url if attempt and fallback_url else url)
        if attempt and (not fallback_url or attempt > 1):
            # Refresh a stale release redirect, retaining the exact source path.
            query = parts.query + ("&" if parts.query else "") + f"miseed_retry={time.time_ns()}"
            parts = parts._replace(query=query)
        request = Request(urlunsplit(parts), headers={"Cache-Control": "no-cache", "Accept": "application/octet-stream"})
        tmp = None
        try:
            with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".miseed-download-",
                                             delete=False) as stream:
                tmp = Path(stream.name)
                checksum = hashlib.sha256()
                with urlopen(request, timeout=30) as response:
                    while chunk := response.read(1024 * 1024):
                        stream.write(chunk)
                        checksum.update(chunk)
            if checksum.hexdigest() != digest:
                raise ValueError("download SHA-256 mismatch; input rejected")
            os.replace(tmp, output)
            tmp = None
            return
        except (URLError, TimeoutError, ConnectionError, IncompleteRead) as error:
            transient = not isinstance(error, HTTPError) or error.code in (408, 429, 500, 502, 503, 504)
            if not transient or attempt == 2:
                raise
            print(f"reference download transport failure; retry {attempt + 1}/2", flush=True)
            time.sleep(attempt + 1)
        finally:
            if tmp is not None:
                tmp.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--fallback-url", help="reviewed alternate endpoint for the same hash-locked asset")
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    download(args.url, args.sha256, args.output, args.fallback_url)


if __name__ == "__main__":
    main()
