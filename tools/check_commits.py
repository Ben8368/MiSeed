"""Validate only non-merge commits introduced by an explicit PR range."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import re
import subprocess
from pathlib import Path


AI_AUTHORS = {
    "Codex <codex@openai.com>",
    "Claude Code <noreply@anthropic.com>",
    "Cursor <cursoragent@cursor.com>",
    "Google Labs Jules <161369871+google-labs-jules[bot]@users.noreply.github.com>",
}
TITLE = re.compile(r"(?:feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)"
                   r"(?:\([^\n()]+\))?!?: .+")


def git(root, *args, input_text=None):
    result = subprocess.run(
        ["git", "-C", str(root), *args], input=input_text, capture_output=True,
        text=True, encoding="utf-8", check=True,
    )
    return result.stdout


def message_errors(message, trailers):
    lines = message.splitlines()
    title = lines[0] if lines else ""
    errors = []
    if not TITLE.fullmatch(title) or not re.search(r"[\u3400-\u9fff]", title):
        errors.append("expected Conventional Commit type and Chinese title")
    parsed = []
    for line in trailers.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            parsed.append((key.lower(), value.strip()))
    human = [value for key, value in parsed if key == "human-authored"]
    ai = [value for key, value in parsed if key == "co-authored-by" and value in AI_AUTHORS]
    bot = [value for key, value in parsed if key == "bot-authored-by"]
    if human:
        if human != ["true"] or ai or bot:
            errors.append("Human-authored: true must be exclusive of AI/bot attribution")
    elif not ai and not any(bot):
        errors.append("missing recognized AI, bot, or Human-authored trailer")
    return errors


def check_range(root, base, head):
    for identity in (base, head):
        if not re.fullmatch(r"[0-9a-f]{40}", identity) or identity == "0" * 40:
            raise ValueError("base/head must be full nonzero commit SHAs")
        git(root, "cat-file", "-e", identity + "^{commit}")
    commits = git(root, "rev-list", "--no-merges", f"{base}..{head}").splitlines()
    errors = []
    for commit in commits:
        message = git(root, "show", "-s", "--format=%B", commit)
        trailers = git(root, "interpret-trailers", "--parse", input_text=message)
        errors.extend(f"{commit[:12]}: {error}" for error in message_errors(message, trailers))
    return commits, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", required=True)
    args = parser.parse_args()
    try:
        commits, errors = check_range(args.root, args.base, args.head)
    except (ValueError, subprocess.CalledProcessError) as exc:
        print("ERROR:", exc)
        return 1
    for error in errors:
        print("ERROR:", error)
    if errors:
        return 1
    print(f"PASS: {len(commits)} new non-merge commit(s); historical commits untouched")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
