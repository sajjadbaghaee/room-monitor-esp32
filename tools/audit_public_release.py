#!/usr/bin/env python3
"""Conservative pre-publication scan for credentials, real endpoints and keys.

This is not a complete secret scanner. Review every git diff and use GitHub
secret scanning / an established secret scanner as additional controls.
"""
from __future__ import annotations

import ipaddress
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_DIRS = {".git", ".pio", ".vscode", "__pycache__", ".idea"}
BAD_FILENAMES = {"Secrets.h", ".env", "id_rsa", "id_ed25519"}
BAD_SUFFIXES = {".pem", ".key", ".p12", ".pfx", ".bin", ".elf"}


def public_file(p: Path) -> bool:
    if any(part in EXCLUDED_DIRS for part in p.relative_to(ROOT).parts):
        return False
    # Secrets.h exists on a developer workstation and is legitimately Git-ignored.
    return p.name != "Secrets.h"


def files_to_scan():
    if (ROOT / ".git").exists():
        # Check the Git index to avoid scanning legitimate ignored local data.
        result = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT, capture_output=True)
        if result.returncode == 0 and result.stdout:
            yield from (ROOT / p.decode() for p in result.stdout.split(b"\x00") if p)
            return
    yield from (p for p in ROOT.rglob("*") if p.is_file() and public_file(p))


def main() -> int:
    failures = []
    count = 0
    for path in files_to_scan():
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT)
        count += 1
        if path.name in BAD_FILENAMES or path.suffix.lower() in BAD_SUFFIXES:
            failures.append(f"Forbidden file in public tree: {relative}")
            continue
        try:
            content = path.read_text(encoding="utf-8")
        except (UnicodeError, OSError):
            failures.append(f"Cannot inspect possible binary file: {relative}")
            continue
        if re.search(r"-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----", content):
            failures.append(f"Embedded private key: {relative}")
        if re.search(r'(?im)^\s*#define\s+ROOM_MONITOR_ADMIN_PASSWORD\s+"(?!REPLACE_|\{password\})[^"]+"', content):
            failures.append(f"Bootstrap password embedded in public file: {relative}")
        if re.search(r"(?i)(?:g" + r"hp_|github_" + r"pat_|AK" + r"IA[0-9A-Z]{16}|sk-" + r"[A-Za-z0-9]{20,})", content):
            failures.append(f"Potential token or API key: {relative}")
        if relative == Path("include/AppConfig.h"):
            if "broker.example.invalid" not in content or "CONFIGURE_TEMPERATURE_ID" not in content:
                failures.append("Expected sanitized public default configuration is missing")
        for match in re.finditer(r"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])", content):
            try:
                ip = ipaddress.ip_address(match.group())
            except ValueError:
                continue
            if ip.is_global:  # public production endpoints are not permitted
                failures.append(f"Public IP literal in {relative}")
                break
    if failures:
        print("Publication safety scan FAILED:")
        for issue in sorted(set(failures)):
            print(" -", issue)
        return 1
    print(f"Publication safety scan passed ({count} files checked; heuristic only).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
