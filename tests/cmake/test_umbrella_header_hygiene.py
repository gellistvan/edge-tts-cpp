#!/usr/bin/env python3
"""
Hygiene tests for include/edge_tts/edge_tts.hpp (the public umbrella header).

Verifies:
  1. The umbrella header exists.
  2. It has a #pragma once guard.
  3. It includes the required stable public API headers.
  4. It does NOT include forbidden modules:
       cli/        — CLI argument parsing; app-only
       media/      — ffplay/ffmpeg process runner; app-only
       communication/  — internal transport; not needed for basic TTS
       serialization/  — internal protocol framing; not needed for basic TTS
       Fake*.hpp       — test-only doubles; must never appear in public headers
       test_support    — test-only infrastructure
  5. docs/MODULES.md references edge_tts/edge_tts.hpp as the umbrella.
  6. README.md uses #include <edge_tts/edge_tts.hpp> in its examples.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
UMBRELLA = REPO_ROOT / "include" / "edge_tts" / "edge_tts.hpp"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# 1. Umbrella header exists
# ---------------------------------------------------------------------------

def test_umbrella_exists() -> None:
    if not UMBRELLA.exists():
        fail(
            f"include/edge_tts/edge_tts.hpp does not exist.\n"
            "Create it as the public umbrella header for TTS consumers."
        )
    ok("include/edge_tts/edge_tts.hpp exists")


# ---------------------------------------------------------------------------
# 2. pragma once guard
# ---------------------------------------------------------------------------

def test_umbrella_has_pragma_once() -> None:
    content = read(UMBRELLA)
    if "#pragma once" not in content:
        fail("include/edge_tts/edge_tts.hpp is missing #pragma once")
    ok("include/edge_tts/edge_tts.hpp has #pragma once")


# ---------------------------------------------------------------------------
# 3. Required stable headers are included
# ---------------------------------------------------------------------------

REQUIRED_INCLUDES = [
    ("edge_tts/version.hpp",          "version macros and constexpr values"),
    ("api/SpeechSynthesizer.hpp",     "synthesis facade"),
    ("api/SynthesisOptions.hpp",      "transport options"),
    ("core/TtsConfig.hpp",            "speech configuration"),
    ("common/Error.hpp",              "error types"),
    ("common/Result.hpp",             "result propagation"),
    ("core/Voice.hpp",                "voice type for listings"),
]


def test_umbrella_includes_required_headers() -> None:
    content = read(UMBRELLA)
    missing = []
    for header, description in REQUIRED_INCLUDES:
        # Match both #include "..." and #include <...> forms
        if not re.search(
            rf'#\s*include\s+[<"]{re.escape(header)}[>"]',
            content
        ):
            missing.append(f"{header}  ({description})")

    if missing:
        fail(
            "include/edge_tts/edge_tts.hpp is missing required stable headers:\n  "
            + "\n  ".join(missing)
        )
    ok(f"Umbrella header includes all {len(REQUIRED_INCLUDES)} required stable headers")


# ---------------------------------------------------------------------------
# 4. Forbidden includes are absent
# ---------------------------------------------------------------------------

FORBIDDEN_PATTERNS = [
    # (pattern, reason)
    (r'#\s*include\s+[<"]cli/',
     "CLI headers are app-layer only; not part of the TTS library API"),
    (r'#\s*include\s+[<"]media/',
     "media headers pull in ffplay/ffmpeg; not needed for synthesis"),
    (r'#\s*include\s+[<"]communication/',
     "communication headers are internal transport; expose only via edge_tts::api"),
    (r'#\s*include\s+[<"]serialization/',
     "serialization headers are internal protocol framing; expose only via edge_tts::api"),
    (r'#\s*include\s+[<"][^>\"]*Fake[A-Z][^>\"]*\.hpp[>"]',
     "Fake* headers are test-only utilities; must never appear in public headers"),
    (r'#\s*include\s+[<"][^>\"]*test_support[^>\"]*[>"]',
     "test_support is a test-only CMake target; must not be #included in public headers"),
]


def test_umbrella_excludes_forbidden_headers() -> None:
    content = read(UMBRELLA)
    violations = []
    for pattern, reason in FORBIDDEN_PATTERNS:
        if re.search(pattern, content):
            # Find the matching line for a useful message
            for lineno, line in enumerate(content.splitlines(), 1):
                if re.search(pattern, line):
                    violations.append(f"  line {lineno}: {line.strip()}\n  → {reason}")
    if violations:
        fail(
            "include/edge_tts/edge_tts.hpp includes forbidden headers:\n"
            + "\n".join(violations)
        )
    ok("Umbrella header excludes all forbidden modules (cli, media, communication, serialization, Fake*, test_support)")


# ---------------------------------------------------------------------------
# 5. docs/MODULES.md documents the umbrella header
# ---------------------------------------------------------------------------

def test_modules_md_documents_umbrella() -> None:
    modules_md = REPO_ROOT / "docs" / "MODULES.md"
    if not modules_md.exists():
        fail("docs/MODULES.md does not exist")
    content = read(modules_md)
    if "edge_tts/edge_tts.hpp" not in content and "edge_tts.hpp" not in content:
        fail(
            "docs/MODULES.md does not mention edge_tts/edge_tts.hpp.\n"
            "Add documentation for the umbrella header in the public consumer targets section."
        )
    ok("docs/MODULES.md documents edge_tts/edge_tts.hpp")


# ---------------------------------------------------------------------------
# 6. README.md uses the umbrella header in examples
# ---------------------------------------------------------------------------

def test_readme_uses_umbrella_header() -> None:
    readme = REPO_ROOT / "README.md"
    if not readme.exists():
        fail("README.md does not exist")
    content = read(readme)
    if "#include" not in content or "edge_tts/edge_tts.hpp" not in content:
        fail(
            "README.md does not show '#include <edge_tts/edge_tts.hpp>' in examples.\n"
            "Update the Usage section to use the umbrella header."
        )
    ok("README.md shows #include <edge_tts/edge_tts.hpp>")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_umbrella_exists,
        test_umbrella_has_pragma_once,
        test_umbrella_includes_required_headers,
        test_umbrella_excludes_forbidden_headers,
        test_modules_md_documents_umbrella,
        test_readme_uses_umbrella_header,
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} umbrella header hygiene checks passed.")


if __name__ == "__main__":
    main()
