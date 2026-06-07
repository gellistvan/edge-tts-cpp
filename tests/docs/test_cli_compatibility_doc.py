#!/usr/bin/env python3
"""Smoke test: verifies docs/CLI_COMPATIBILITY.md structure and content.

Checks:
  1. docs/CLI_COMPATIBILITY.md exists.
  2. Both 'edge-tts' and 'edge-playback' are mentioned.
  3. At least ten option rows (table rows containing '|') are present.
  4. Stale phrases are absent.
  5. Proxy support status is explicitly documented.
  6. Platform (Windows) support status is explicitly documented.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
CLI_COMPAT = REPO_ROOT / "docs" / "CLI_COMPATIBILITY.md"

MIN_OPTION_ROWS = 10

# Stale phrases that must NOT appear in CLI_COMPATIBILITY.md
STALE_PHRASES = [
    ("planned C++ `edge-tts-cpp` CLI", "stale 'planned CLI' reference — CLI is implemented"),
    ("C++ planned behavior", "stale column header — should be 'C++ behavior'"),
    ("WebSocket transport not yet implemented", "stale transport status"),
]

# Phrases that MUST appear (proxy status and platform support explicitly documented)
REQUIRED_PHRASES = [
    ("unsupported", "proxy or Windows support must be marked 'unsupported'"),
    ("Windows", "platform support (Windows) must be mentioned"),
    ("proxy", "proxy support status must be mentioned"),
]


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    # 1. File existence
    if not CLI_COMPAT.exists():
        fail(f"{CLI_COMPAT} does not exist")

    content = CLI_COMPAT.read_text(encoding="utf-8")

    # 2. Both commands mentioned
    for cmd in ("edge-tts", "edge-playback"):
        if cmd not in content:
            fail(f"'{cmd}' not mentioned in CLI_COMPATIBILITY.md")

    # 3. Count option rows: table body rows that contain a pipe character and
    #    an option-like entry (starts with `|` and has at least one `--` or `-X`)
    option_rows = [
        line for line in content.splitlines()
        if re.match(r"^\s*\|", line) and "--" in line
    ]
    if len(option_rows) < MIN_OPTION_ROWS:
        fail(
            f"Expected at least {MIN_OPTION_ROWS} option rows in CLI_COMPATIBILITY.md, "
            f"found {len(option_rows)}"
        )

    # 4. Stale-phrase check
    for phrase, description in STALE_PHRASES:
        if phrase in content:
            fail(
                f"Stale phrase found in CLI_COMPATIBILITY.md ({description}):\n"
                f"  '{phrase}'"
            )

    # 5. Required content check (proxy status, platform support)
    for phrase, description in REQUIRED_PHRASES:
        if phrase.lower() not in content.lower():
            fail(
                f"Required content missing from CLI_COMPATIBILITY.md ({description}):\n"
                f"  '{phrase}'"
            )

    print(
        f"OK: CLI_COMPATIBILITY.md found, both commands mentioned, "
        f"{len(option_rows)} option rows present. "
        f"No stale phrases found. Proxy and platform support documented."
    )


if __name__ == "__main__":
    main()
