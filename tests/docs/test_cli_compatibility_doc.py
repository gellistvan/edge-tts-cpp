#!/usr/bin/env python3
"""Smoke test: verifies docs/CLI_COMPATIBILITY.md structure and content.

Checks:
  1. docs/CLI_COMPATIBILITY.md exists.
  2. Both 'edge-tts' and 'edge-playback' are mentioned.
  3. At least ten option rows (table rows containing '|') are present.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
CLI_COMPAT = REPO_ROOT / "docs" / "CLI_COMPATIBILITY.md"

MIN_OPTION_ROWS = 10


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

    print(
        f"OK: CLI_COMPATIBILITY.md found, both commands mentioned, "
        f"{len(option_rows)} option rows present."
    )


if __name__ == "__main__":
    main()
