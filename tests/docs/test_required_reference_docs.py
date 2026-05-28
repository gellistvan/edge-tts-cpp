#!/usr/bin/env python3
"""Smoke test: verifies docs/REFERENCE_BEHAVIOR.md structure and content.

Checks:
  1. docs/REFERENCE_BEHAVIOR.md exists.
  2. All required section headings are present.
  3. At least five distinct reference/src/edge_tts/... file paths are mentioned.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

# ---------------------------------------------------------------------------
# Locate the repo root: two levels up from this file (tests/docs/ → repo root)
# ---------------------------------------------------------------------------
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
REFERENCE_BEHAVIOR = REPO_ROOT / "docs" / "REFERENCE_BEHAVIOR.md"

REQUIRED_HEADINGS = [
    "# Reference Files Inspected",
    "# Public Python API",
    "# CLI Commands",
    "# CLI Options",
    "# Default Values",
    "# Voice Listing",
    "# Text Validation",
    "# Text Chunking",
    "# SSML Generation",
    "# WebSocket Protocol",
    "# DRM / Sec-MS-GEC",
    "# Stream Chunk Types",
    "# Subtitles",
    "# Playback",
    "# Proxy Behavior",
    "# Retry / Clock Skew Behavior",
    "# Error Behavior",
    "# Compatibility Targets",
    "# Ambiguities / Requires Live Verification",
]

MIN_REFERENCE_PATHS = 5
REFERENCE_PATH_PATTERN = re.compile(
    r"reference/(?:edge-tts/)?src/edge_tts/[^\s`\)\"']+"
)


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    # 1. File existence
    if not REFERENCE_BEHAVIOR.exists():
        fail(f"{REFERENCE_BEHAVIOR} does not exist")

    content = REFERENCE_BEHAVIOR.read_text(encoding="utf-8")

    # 2. Required headings
    missing = [h for h in REQUIRED_HEADINGS if h not in content]
    if missing:
        fail(
            "Missing required headings in REFERENCE_BEHAVIOR.md:\n  "
            + "\n  ".join(missing)
        )

    # 3. At least five distinct reference/src/edge_tts/... paths mentioned
    found_paths = set(REFERENCE_PATH_PATTERN.findall(content))
    if len(found_paths) < MIN_REFERENCE_PATHS:
        fail(
            f"Expected at least {MIN_REFERENCE_PATHS} distinct "
            f"'reference/src/edge_tts/...' paths in REFERENCE_BEHAVIOR.md, "
            f"found {len(found_paths)}: {sorted(found_paths)}"
        )

    print(
        f"OK: REFERENCE_BEHAVIOR.md found, {len(REQUIRED_HEADINGS)} headings present, "
        f"{len(found_paths)} reference paths mentioned."
    )


if __name__ == "__main__":
    main()
