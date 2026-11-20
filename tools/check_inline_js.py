#!/usr/bin/env python3
"""Extract inline browser scripts and ask Node.js to syntax-check them."""
from __future__ import annotations
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NODE = shutil.which("node")
if not NODE:
    print("node not found; JavaScript syntax check skipped")
    sys.exit(0)

files = [
    ROOT / "data/index.html",
    ROOT / "data/measurements.html",
    ROOT / "data/configuration.html",
    ROOT / "src/WifiSettingsPage.h",
    ROOT / "src/SystemUpdatePage.h",
    ROOT / "data/diagnostics.html",
    ROOT / "src/AdminLoginPage.h",
]
failed = False
with tempfile.TemporaryDirectory() as td:
    for path in files:
        content = path.read_text(encoding="utf-8", errors="replace")
        scripts = re.findall(r"<script[^>]*>(.*?)</script>", content, re.S | re.I)
        for idx, script in enumerate(scripts):
            temp = Path(td) / f"{path.stem}_{idx}.js"
            temp.write_text(script, encoding="utf-8")
            proc = subprocess.run([NODE, "--check", str(temp)], text=True, capture_output=True)
            if proc.returncode:
                failed = True
                print(f"JavaScript syntax failure: {path.relative_to(ROOT)} script {idx}")
                print(proc.stderr.strip())

# Validate the firmware-resident OTA SHA-256 implementation against the
# standard "abc" test vector. This is intentionally a semantic check rather
# than only a JavaScript syntax check because verified OTA depends on it.
system_page = (ROOT / "src/SystemUpdatePage.h").read_text(encoding="utf-8", errors="replace")
start = system_page.find("function ror")
end = system_page.find("function fileSha256")
if start < 0 or end <= start:
    print("Could not locate OTA SHA-256 implementation")
    failed = True
else:
    sha_code = system_page[start:end]
    vector_script = sha_code + "\nconst b=new TextEncoder().encode('abc'); console.log(sha256(b.buffer));\n"
    with tempfile.TemporaryDirectory() as vector_td:
        vector_file = Path(vector_td) / "ota_sha_vector.js"
        vector_file.write_text(vector_script, encoding="utf-8")
        proc = subprocess.run([NODE, str(vector_file)], text=True, capture_output=True)
    expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    if proc.returncode or proc.stdout.strip() != expected:
        failed = True
        print("OTA browser SHA-256 test vector FAILED")
        print((proc.stderr or proc.stdout).strip())

if failed:
    sys.exit(1)
print("Inline JavaScript syntax + OTA SHA-256 validation passed.")
