#!/usr/bin/env python3
"""Smoke test: verifies docs/REFERENCE_BEHAVIOR.md structure and content.

Checks:
  1. docs/REFERENCE_BEHAVIOR.md exists.
  2. All required section headings are present.
  3. At least five distinct reference/src/edge_tts/... file paths are mentioned.
  4. Stale implementation phrases are absent from doc files.
  5. Key CMake option names are mentioned in DEPENDENCIES.md and TESTING.md.

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

# ---------------------------------------------------------------------------
# Stale phrases that must NOT appear in doc files under docs/
# (phrase, description for error message)
# ---------------------------------------------------------------------------
STALE_PHRASES = [
    ("WebSocket and real networking stubs remain", "stale communication-module status"),
    ("production synthesis is stubbed", "stale production-stub claim"),
    ("FakeHttpClient is used by production app", "stale FakeHttpClient production claim"),
]

# ---------------------------------------------------------------------------
# Required CMake variable mentions in specific doc files
# ---------------------------------------------------------------------------
REQUIRED_IN_DEPS_MD = [
    "EDGE_TTS_FETCH_DEPS",
    "EDGE_TTS_REQUIRE_NETWORKING",
]
REQUIRED_IN_TESTING_MD = [
    "EDGE_TTS_ENABLE_NETWORK_TESTS",
    "EDGE_TTS_RUN_NETWORK_TESTS",
]


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

    # 4. Stale-phrase check across all *.md files under docs/
    docs_dir = REPO_ROOT / "docs"
    for md_file in sorted(docs_dir.glob("*.md")):
        file_content = md_file.read_text(encoding="utf-8")
        for phrase, description in STALE_PHRASES:
            if phrase in file_content:
                fail(
                    f"Stale phrase found in {md_file.name} ({description}):\n"
                    f"  '{phrase}'"
                )

    # 5. Required CMake variable mentions
    deps_md = REPO_ROOT / "docs" / "DEPENDENCIES.md"
    if deps_md.exists():
        deps_content = deps_md.read_text(encoding="utf-8")
        for var in REQUIRED_IN_DEPS_MD:
            if var not in deps_content:
                fail(f"DEPENDENCIES.md does not mention CMake option '{var}'")

    testing_md = REPO_ROOT / "docs" / "TESTING.md"
    if testing_md.exists():
        testing_content = testing_md.read_text(encoding="utf-8")
        for var in REQUIRED_IN_TESTING_MD:
            if var not in testing_content:
                fail(f"TESTING.md does not mention CMake/env option '{var}'")

    print(
        f"OK: REFERENCE_BEHAVIOR.md found, {len(REQUIRED_HEADINGS)} headings present, "
        f"{len(found_paths)} reference paths mentioned. "
        f"No stale phrases found. Required CMake options documented."
    )


if __name__ == "__main__":
    main()
