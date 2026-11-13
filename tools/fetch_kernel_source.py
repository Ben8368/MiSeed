#!/usr/bin/env python3
"""Fetch the locked Linux revision, falling back only to its pinned mirror tag."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


def git(cache, *args, timeout=300):
    return subprocess.run(
        ["git", "-C", str(cache), *args], check=True, capture_output=True,
        text=True, encoding="utf-8", timeout=timeout,
        env={**os.environ, "GIT_TERMINAL_PROMPT": "0", "GIT_NO_LAZY_FETCH": "1"},
    ).stdout.strip()


def ensure_locked_source(cache, kernel_url, mirror_url, tag, tag_object, revision):
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
        raise ValueError("invalid Linux release tag")
    if any(not re.fullmatch(r"[0-9a-f]{40}", value)
           for value in (tag_object, revision)):
        raise ValueError("invalid locked Linux object id")

    cache = Path(cache).resolve()
    if not cache.exists():
        git(cache.parent, "init", "--bare", str(cache))
        git(cache, "remote", "add", "origin", kernel_url)
    remote_url = git(cache, "remote", "get-url", "origin")
    if remote_url not in {kernel_url, mirror_url}:
        raise ValueError("Linux source cache has an unreviewed origin URL")
    try:
        git(cache, "cat-file", "-e", f"{revision}^{{commit}}")
        present = True
    except subprocess.CalledProcessError:
        present = False
    if not present and remote_url == kernel_url:
        try:
            git(cache, "fetch", "--depth=1", "--filter=blob:none", "origin", revision,
                timeout=60)
            present = True
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            details = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
            print(f"warning: primary source unavailable; retrying locked mirror: {details}",
                  file=sys.stderr)
    if not present:
        git(cache, "remote", "set-url", "origin", mirror_url)
        remote_url = mirror_url
        git(cache, "fetch", "--depth=1", "--filter=blob:none", "origin",
            f"+refs/tags/{tag}:refs/miseed/{tag}")
    if remote_url == mirror_url:
        ref = f"refs/miseed/{tag}"
        if git(cache, "rev-parse", ref) != tag_object:
            raise ValueError("Linux mirror tag object mismatch")
        if git(cache, "rev-parse", f"{ref}^{{commit}}") != revision:
            raise ValueError("Linux mirror commit mismatch")

    actual_revision = git(cache, "rev-parse", f"{revision}^{{commit}}")
    if actual_revision != revision:
        raise ValueError("Linux source revision mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--kernel-url", required=True)
    parser.add_argument("--mirror-url", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--tag-object", required=True)
    parser.add_argument("--revision", required=True)
    args = parser.parse_args()
    try:
        ensure_locked_source(args.cache, args.kernel_url, args.mirror_url,
                             args.tag, args.tag_object, args.revision)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        details = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        print(f"ERROR: {details}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
