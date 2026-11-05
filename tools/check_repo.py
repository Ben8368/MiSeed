"""Offline repository checks. No downloads, builds, or device access."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import json
import re
import subprocess
from pathlib import Path
from urllib.parse import unquote, urlsplit


BUDGETS = {
    "AGENTS.md": (45, 2200),
    "README.md": (45, 2200),
    "docs/CONTEXT.md": (25, 1200),
    "docs/LESSONS.md": (12, 600),
}
DOC_BUDGET = (60, 3500)
ADR_BUDGET = (30, 1800)
REQUIRED = (
    "README.md", "AGENTS.md", "docs/CONTEXT.md", "docs/LESSONS.md",
    "docs/PRODUCT.md", "docs/ARCHITECTURE.md", "docs/platforms.md",
    "docs/PROVENANCE.md", "docs/CI.md",
    "docs/QUALITY.md", "docs/GOVERNANCE.md", "docs/AI_RULES.md",
    "docs/RISK_REGISTER.md", "docs/TECH_DEBT.md",
    "docs/implementation-plan.md", "docs/ADR/README.md",
    "manifests/sources.json", ".github/workflows/ci.yml",
)
EXCLUDED = {".git", ".work", "out", "local", ".venv", "__pycache__"}
TEXT_SUFFIXES = {".md", ".py", ".json", ".yml", ".yaml", ".toml", ".sh"}
FORBIDDEN_SUFFIXES = {
    ".img", ".bin", ".elf", ".dtb", ".dtbo", ".cpio", ".log", ".pem", ".key",
    ".pyc", ".pyo",
}


def repository_files(root):
    """Include tracked files even when ignored; also inspect untracked work."""
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
         "--exclude-standard"], capture_output=True, check=False,
    )
    if result.returncode == 0:
        return sorted({root / name for name in result.stdout.decode("utf-8").split("\0")
                       if name and ((root / name).exists() or (root / name).is_symlink())})
    return sorted(p for p in root.rglob("*") if (p.is_file() or p.is_symlink())
                  and not EXCLUDED.intersection(p.relative_to(root).parts))


def document_budget(name):
    if name in BUDGETS:
        return BUDGETS[name]
    if name.startswith("docs/") and name.endswith(".md"):
        if name.startswith("docs/archive/"):
            return None
        return ADR_BUDGET if name.startswith("docs/ADR/") else DOC_BUDGET
    return None


def text_errors(name, data):
    errors = []
    try:
        content = data.decode("utf-8")
    except UnicodeDecodeError:
        return [f"{name}: invalid UTF-8"], None
    if content.startswith("\ufeff"):
        errors.append(f"{name}: UTF-8 BOM is forbidden")
    if "\r" in content:
        errors.append(f"{name}: use LF line endings")
    if content and not content.endswith("\n"):
        errors.append(f"{name}: missing final newline")
    for number, line in enumerate(content.splitlines(), 1):
        if line.rstrip(" \t") != line:
            errors.append(f"{name}:{number}: trailing whitespace")
    budget = document_budget(name)
    if budget is not None:
        lines, chars = budget
        if len(content.splitlines()) > lines or len(content) > chars:
            errors.append(f"{name}: exceeds budget ({lines} lines / {chars} characters)")
    return errors, content


def markdown_errors(root, path, content):
    """Check inline relative file links; anchors and reference links are manual."""
    root = root.resolve()
    path = path.resolve()
    errors = []
    content = re.sub(r"^```[^\n]*\n.*?^```\s*$", "", content,
                     flags=re.MULTILINE | re.DOTALL)
    for match in re.finditer(r"\[[^\]\n]+\]\((<[^>]+>|[^\s)]+)\)", content):
        target = match.group(1).strip("<>")
        parsed = urlsplit(target)
        if parsed.scheme or target.startswith("#"):
            continue
        if target.startswith(("/", "\\")):
            errors.append(f"{path.relative_to(root)}: use relative link: {target}")
            continue
        destination = (path.parent / unquote(parsed.path)).resolve()
        if not destination.is_relative_to(root) or not destination.exists():
            errors.append(f"{path.relative_to(root)}: broken/escaping link: {target}")
    return errors


def sources_errors(data):
    """Candidates cannot silently become a build lock."""
    if not isinstance(data, dict) or set(data) != {"schema_version", "candidates"}:
        return ["sources.json: expected schema_version and candidates"]
    if type(data["schema_version"]) is not int or data["schema_version"] != 1:
        return ["sources.json: unsupported schema_version"]
    candidates = data["candidates"]
    if not isinstance(candidates, list) or not candidates:
        return ["sources.json: candidates must be a nonempty list"]
    errors, seen = [], set()
    fields = {"id", "url", "purpose", "status", "revision", "license_review"}
    for item in candidates:
        if not isinstance(item, dict) or set(item) != fields:
            errors.append("sources.json: invalid candidate fields")
            continue
        identity = item["id"]
        if not isinstance(identity, str) or not re.fullmatch(r"[a-z0-9-]+", identity):
            errors.append("sources.json: invalid id")
        elif identity in seen:
            errors.append(f"sources.json: duplicate id {identity}")
        else:
            seen.add(identity)
        url = item["url"]
        if not isinstance(url, str) or not re.fullmatch(r"https://[^\s]+", url):
            errors.append(f"sources.json: {identity}: expected HTTPS URL")
        if not isinstance(item["purpose"], str) or not item["purpose"].strip():
            errors.append(f"sources.json: {identity}: missing purpose")
        if (item["status"] != "candidate" or item["revision"] is not None
                or item["license_review"] != "pending"):
            errors.append(f"sources.json: {identity}: use a reviewed lock for imports")
    return errors


def adr_errors(root, texts):
    errors, seen = [], set()
    index = texts.get("docs/ADR/README.md", "")
    entries = re.findall(r"\| \[(\d{4})\]\(([^)]+)\) \| ([^|]+) \|", index)
    for number, filename, status in entries:
        if number in seen:
            errors.append(f"ADR: duplicate number {number}")
        seen.add(number)
        name = "docs/ADR/" + filename
        content = texts.get(name, "")
        if not filename.startswith(number + "-") or f"- 状态：{status.strip()}" not in content:
            errors.append(f"ADR: index/status mismatch for {number}")
        for target in re.findall(r"替代：\s*\[.*?\]\(([^)]+)\)", content):
            if target == filename or not (root / "docs/ADR" / target).is_file():
                errors.append(f"ADR: invalid replacement for {number}")
    for name in texts:
        match = re.fullmatch(r"docs/ADR/(\d{4})-.+\.md", name)
        if match and match.group(1) not in seen:
            errors.append(f"ADR: unindexed {name}")
    if not entries:
        errors.append("ADR: empty index")
    return errors


def check_repository(root):
    root = root.resolve()
    errors, texts = [], {}
    for name in REQUIRED:
        if not (root / name).is_file():
            errors.append(f"missing required file: {name}")
    for path in repository_files(root):
        name = path.relative_to(root).as_posix()
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            errors.append(f"{name}: symlink/escaping file is forbidden")
            continue
        if path.relative_to(root).parts[0] in EXCLUDED:
            errors.append(f"{name}: tracked local/generated directory is forbidden")
            continue
        if (path.suffix.lower() in FORBIDDEN_SUFFIXES
                or path.name == ".env" or path.name.startswith(".env.")):
            errors.append(f"{name}: forbidden artifact or sensitive input")
            continue
        if path.stat().st_size > 1_000_000:
            errors.append(f"{name}: oversized file; review outside Git")
            continue
        if path.suffix in TEXT_SUFFIXES or path.name.startswith("."):
            found, content = text_errors(name, path.read_bytes())
            errors.extend(found)
            if content is not None:
                texts[name] = content
                if path.suffix == ".md":
                    errors.extend(markdown_errors(root, path, content))
    manifest = texts.get("manifests/sources.json")
    if manifest is not None:
        try:
            errors.extend(sources_errors(json.loads(manifest)))
        except json.JSONDecodeError as exc:
            errors.append(f"sources.json: invalid JSON: {exc}")
    errors.extend(adr_errors(root, texts))
    context = texts.get("docs/CONTEXT.md", "")
    priorities = re.search(r"## 近期优先级\n(.*?)(?=\n## |\Z)", context, re.DOTALL)
    if priorities is None or len(re.findall(r"^\d+\. ", priorities.group(1), re.MULTILINE)) > 3:
        errors.append("CONTEXT: expected at most three numbered priorities")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors = check_repository(args.root)
    for error in errors:
        print("ERROR:", error)
    if errors:
        return 1
    print("PASS: repository text, links, budgets, ADRs and source candidates (L0 only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
