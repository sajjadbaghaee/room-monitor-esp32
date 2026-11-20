#!/usr/bin/env python3
"""Generate a local per-build administrator bootstrap credential; never print it.

Usage: python tools/generate_secrets.py
The output must never be committed or uploaded. If the file already exists,
refuse to overwrite it unless the user explicitly removes/backs it up.
"""
from __future__ import annotations

import os
import secrets
from pathlib import Path

output = Path(__file__).resolve().parents[1] / "include" / "Secrets.h"
content = """#pragma once
// GENERATED LOCAL SECRET. NEVER COMMIT THIS FILE.
#define ROOM_MONITOR_SECRETS_CONFIGURED 1
#define ROOM_MONITOR_ADMIN_USERNAME "admin"
#define ROOM_MONITOR_ADMIN_PASSWORD "{password}"
""".format(password=secrets.token_urlsafe(24))

# Exclusive creation prevents accidental credential rotation on reruns.
with output.open("x", encoding="utf-8") as f:
    try:
        os.chmod(output, 0o600)
    except OSError:
        pass
    f.write(content)
print("Created include/Secrets.h (secret value withheld; file is ignored by Git).")
print("Keep a secure private record of the bootstrap password before flashing.")
