#!/usr/bin/env python3
"""Smoke test: verifies docs/REFERENCE_BEHAVIOR.md structure and content.

Checks:
  1. docs/REFERENCE_BEHAVIOR.md exists.
  2. All required section headings are present.
  3. At least five distinct reference/src/edge_tts/... file paths are mentioned.
  4. Stale implementation phrases are absent from doc files.
  5. Key CMake option names are mentioned in DEPENDENCIES.md and TESTING.md.
  6. Broken lowercase link 'docs/high-level-design.md' does not appear anywhere.
  7. README.md contains required links to HIGH_LEVEL_DESIGN.md, TESTING.md,
     and RELEASE_READINESS.md.
  8. RELEASE_READINESS.md contains a recognized maturity label.

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
    ("proxy support implemented", "misleading proxy-support claim (proxy is not functional end-to-end)"),
]

# ---------------------------------------------------------------------------
# Broken-link check: the lowercase form must never appear as a link target
# (the correct casing is docs/HIGH_LEVEL_DESIGN.md)
# ---------------------------------------------------------------------------
BROKEN_LINKS = [
    ("docs/high-level-design.md", "broken link: use docs/HIGH_LEVEL_DESIGN.md (correct case)"),
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

# ---------------------------------------------------------------------------
# RELEASE_READINESS.md must document the subtitle timing model
# ---------------------------------------------------------------------------
REQUIRED_IN_RELEASE_READINESS = [
    "subtitle timing",
]

# ---------------------------------------------------------------------------
# Required links in README.md
# ---------------------------------------------------------------------------
README_REQUIRED_LINKS = [
    ("docs/HIGH_LEVEL_DESIGN.md", "README.md must link to docs/HIGH_LEVEL_DESIGN.md"),
    ("docs/TESTING.md", "README.md must link to docs/TESTING.md"),
    ("docs/RELEASE_READINESS.md", "README.md must link to docs/RELEASE_READINESS.md"),
    ("examples/consumer_add_subdirectory", "README.md must reference examples/consumer_add_subdirectory"),
    ("examples/consumer_find_package", "README.md must reference examples/consumer_find_package"),
]

# ---------------------------------------------------------------------------
# RELEASE_READINESS.md must contain one of these maturity labels (case-insensitive)
# ---------------------------------------------------------------------------
MATURITY_LABELS = [
    "prototype",
    "early alpha",
    "alpha",
    "beta candidate",
    "beta",
    "production-ready",
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
            if phrase.lower() in file_content.lower():
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

    # 5b. RELEASE_READINESS.md must document the subtitle timing known limitation
    release_readiness = REPO_ROOT / "docs" / "RELEASE_READINESS.md"
    if release_readiness.exists():
        rr_text = release_readiness.read_text(encoding="utf-8").lower()
        for phrase in REQUIRED_IN_RELEASE_READINESS:
            if phrase.lower() not in rr_text:
                fail(
                    f"RELEASE_READINESS.md does not mention required topic '{phrase}' "
                    f"in Known limitations"
                )

    # 6. Broken-link check across ALL *.md files in the repo root and docs/
    all_md_files = list(docs_dir.glob("*.md")) + [REPO_ROOT / "README.md"]
    for md_file in sorted(all_md_files):
        if not md_file.exists():
            continue
        file_content = md_file.read_text(encoding="utf-8")
        for broken_link, description in BROKEN_LINKS:
            if broken_link in file_content:
                fail(
                    f"Broken link found in {md_file.name} ({description}):\n"
                    f"  '{broken_link}'"
                )

    # 7. README.md must contain required links
    readme = REPO_ROOT / "README.md"
    if readme.exists():
        readme_content = readme.read_text(encoding="utf-8")
        for link_target, description in README_REQUIRED_LINKS:
            if link_target not in readme_content:
                fail(
                    f"Required link missing from README.md ({description}):\n"
                    f"  '{link_target}'"
                )

    # 8. RELEASE_READINESS.md must contain a maturity label
    release_readiness = REPO_ROOT / "docs" / "RELEASE_READINESS.md"
    if release_readiness.exists():
        rr_content = release_readiness.read_text(encoding="utf-8").lower()
        if not any(label in rr_content for label in MATURITY_LABELS):
            fail(
                f"RELEASE_READINESS.md does not contain a recognized maturity label. "
                f"Expected one of: {MATURITY_LABELS}"
            )

    print(
        f"OK: REFERENCE_BEHAVIOR.md found, {len(REQUIRED_HEADINGS)} headings present, "
        f"{len(found_paths)} reference paths mentioned. "
        f"No stale phrases found. No broken links. Required CMake options documented. "
        f"README links verified. RELEASE_READINESS.md maturity label and known "
        f"limitations present."
    )


if __name__ == "__main__":
    main()
