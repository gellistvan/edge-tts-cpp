#!/usr/bin/env python3
"""
Test suite for tools/check_module_boundaries.py.

Verifies:
  1. Allowed include fixtures produce no violations.
  2. Each forbidden fixture produces at least one violation.
  3. The actual project tree is clean (no violations).

Exit code 0 on success, non-zero on failure.
"""

import importlib.util
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Locate the repo root and import the checker as a module
# ---------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CHECKER_PATH = REPO_ROOT / "tools" / "check_module_boundaries.py"
FIXTURES_ROOT = Path(__file__).resolve().parent / "module_boundary_fixtures"

spec = importlib.util.spec_from_file_location("check_module_boundaries", CHECKER_PATH)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _violations(root: Path) -> list[str]:
    return checker.scan_tree(root)


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


# ---------------------------------------------------------------------------
# Unit tests: module_of_file
# ---------------------------------------------------------------------------

def test_module_of_file_recognition() -> None:
    root = Path("/repo")
    cases = [
        (Path("/repo/include/edge_tts/core/TtsConfig.hpp"),    "core"),
        (Path("/repo/include/edge_tts/common/Errors.hpp"),     "common"),
        (Path("/repo/include/edge_tts/api/Communicate.hpp"),   "api"),
        # Top-level headers directly under include/edge_tts/ are "umbrella"
        (Path("/repo/include/edge_tts/edge_tts.hpp"),          "umbrella"),
        (Path("/repo/src/api/Communicate.cpp"),                "api"),
        (Path("/repo/src/core/TtsConfig.cpp"),                 "core"),
        (Path("/repo/src/subtitles/SrtComposer.cpp"),          "subtitles"),
        (Path("/repo/apps/edge-tts/main.cpp"),                 "apps"),
        (Path("/repo/apps/edge-playback/main.cpp"),            "apps"),
        (Path("/repo/tests/core/test_tts_config.cpp"),         None),
        (Path("/repo/CMakeLists.txt"),                         None),
    ]
    for path, expected in cases:
        got = checker.module_of_file(path, root)
        if got != expected:
            fail(f"module_of_file({path.relative_to(root)}): expected {expected!r}, got {got!r}")
    ok("module_of_file() recognises all path patterns (including umbrella)")


# ---------------------------------------------------------------------------
# Unit tests: violations_in_file (inline content via temp files)
# ---------------------------------------------------------------------------

def _check_content(tmp_path: Path, root: Path, content: str) -> list[str]:
    """Write content to tmp_path and run violations_in_file on it."""
    tmp_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path.write_text(content, encoding="utf-8")
    return checker.violations_in_file(tmp_path, root)


def test_allowed_includes_inline(tmp_path: Path) -> None:
    root = tmp_path

    cases = [
        # (relative_path, content)
        ("include/edge_tts/core/F.hpp",
         '#include "edge_tts/common/Errors.hpp"\n'),
        ("src/serialization/F.cpp",
         '#include "edge_tts/core/TtsConfig.hpp"\n'
         '#include "edge_tts/common/Utf8.hpp"\n'),
        # communication: pure transport — may use serialization/core/common only
        ("src/communication/F.cpp",
         '#include "edge_tts/serialization/EdgeProtocol.hpp"\n'
         '#include "edge_tts/core/TtsConfig.hpp"\n'),
        # api is the public facade — may include communication and everything below
        ("src/api/F.cpp",
         '#include "edge_tts/communication/SynthesisSession.hpp"\n'
         '#include "edge_tts/core/TtsConfig.hpp"\n'),
        # cli may include api headers (Communicate facade)
        ("src/cli/F.cpp",
         '#include "edge_tts/api/Communicate.hpp"\n'),
        ("include/edge_tts/core/Self.hpp",
         '#include "edge_tts/core/TtsConfig.hpp"\n'),  # same-module include
        # apps use api as the primary public entry point
        ("apps/edge-tts/main.cpp",
         '#include "edge_tts/api/Communicate.hpp"\n'),
        # apps may also use cli directly
        ("apps/edge-tts/cli.cpp",
         '#include "edge_tts/cli/CliOptions.hpp"\n'),
        # umbrella header may include api, core, common
        ("include/edge_tts/edge_tts.hpp",
         '#include "edge_tts/api/Communicate.hpp"\n'
         '#include "edge_tts/core/TtsConfig.hpp"\n'
         '#include "edge_tts/common/Result.hpp"\n'),
    ]
    for rel, content in cases:
        path = tmp_path / rel
        viols = _check_content(path, root, content)
        if viols:
            fail(f"Expected no violations in {rel!r}, got:\n  " + "\n  ".join(viols))
    ok("all allowed include patterns pass")


def test_forbidden_includes_inline(tmp_path: Path) -> None:
    root = tmp_path

    cases = [
        # (relative_path, content, expected_violation_substring)
        ("include/edge_tts/common/Bad.hpp",
         '#include "edge_tts/core/TtsConfig.hpp"\n',
         "[common] forbidden include of [core]"),
        ("src/core/Bad.cpp",
         '#include "edge_tts/serialization/EdgeProtocol.hpp"\n',
         "[core] forbidden include of [serialization]"),
        # core must not include api (api is above core)
        ("src/core/Bad2.cpp",
         '#include "edge_tts/api/Communicate.hpp"\n',
         "[core] forbidden include of [api]"),
        # core must not include communication either
        ("src/core/Bad3.cpp",
         '#include "edge_tts/communication/SynthesisSession.hpp"\n',
         "[core] forbidden include of [communication]"),
        ("src/serialization/Bad.cpp",
         '#include "edge_tts/communication/Communicate.hpp"\n',
         "[serialization] forbidden include of [communication]"),
        # serialization must not include api
        ("src/serialization/Bad2.cpp",
         '#include "edge_tts/api/Communicate.hpp"\n',
         "[serialization] forbidden include of [api]"),
        ("src/media/Bad.cpp",
         '#include "edge_tts/communication/Communicate.hpp"\n',
         "[media] forbidden include of [communication]"),
        ("src/media/Bad2.cpp",
         '#include "edge_tts/serialization/EdgeProtocol.hpp"\n',
         "[media] forbidden include of [serialization]"),
        ("src/subtitles/Bad.cpp",
         '#include "edge_tts/communication/Communicate.hpp"\n',
         "[subtitles] forbidden include of [communication]"),
        ("src/subtitles/Bad2.cpp",
         '#include "edge_tts/media/AudioConverter.hpp"\n',
         "[subtitles] forbidden include of [media]"),
        # communication must not include api (api is above communication)
        ("src/communication/Bad.cpp",
         '#include "edge_tts/api/Communicate.hpp"\n',
         "[communication] forbidden include of [api]"),
        ("src/communication/Bad2.cpp",
         '#include "edge_tts/cli/CliOptions.hpp"\n',
         "[communication] forbidden include of [cli]"),
        # communication must not include subtitle or media — those belong in api
        ("src/communication/Bad3.cpp",
         '#include "edge_tts/subtitles/SubMaker.hpp"\n',
         "[communication] forbidden include of [subtitles]"),
        ("src/communication/Bad4.cpp",
         '#include "edge_tts/media/AudioConverter.hpp"\n',
         "[communication] forbidden include of [media]"),
        # cli must not reach past api into communication internals
        ("src/cli/Bad.cpp",
         '#include "edge_tts/communication/SynthesisSession.hpp"\n',
         "[cli] forbidden include of [communication]"),
        # cli must not include serialization directly
        ("src/cli/Bad2.cpp",
         '#include "edge_tts/serialization/TextChunker.hpp"\n',
         "[cli] forbidden include of [serialization]"),
        # umbrella header must not include cli, media, communication, or serialization
        ("include/edge_tts/edge_tts.hpp",
         '#include "edge_tts/cli/CliOptions.hpp"\n',
         "[umbrella] forbidden include of [cli]"),
    ]
    for rel, content, expected_sub in cases:
        path = tmp_path / rel
        viols = _check_content(path, root, content)
        if not viols:
            fail(f"Expected at least one violation in {rel!r}, got none")
        joined = " ".join(viols)
        if expected_sub not in joined:
            fail(
                f"Expected violation containing {expected_sub!r} in {rel!r}, "
                f"got:\n  " + "\n  ".join(viols)
            )
    ok("all forbidden include patterns are detected")


def test_private_header_from_app_inline(tmp_path: Path) -> None:
    root = tmp_path
    path = tmp_path / "apps/edge-tts/main.cpp"
    content = '#include "../../src/core/SomeInternal.hpp"\n'
    viols = _check_content(path, root, content)
    if not viols:
        fail("Expected private-header violation in apps/edge-tts/main.cpp, got none")
    if "private header" not in " ".join(viols).lower():
        fail(f"Expected 'private header' in violation message, got: {viols}")
    ok("private header include from apps/ is detected")


def test_app_public_include_is_allowed_inline(tmp_path: Path) -> None:
    root = tmp_path
    path = tmp_path / "apps/edge-tts/main.cpp"
    content = '#include "edge_tts/api/Communicate.hpp"\n'
    viols = _check_content(path, root, content)
    if viols:
        fail(f"App including public api header should be allowed, got: {viols}")
    ok("app including public api::Communicate header is allowed")


# ---------------------------------------------------------------------------
# Fixture-tree tests
# ---------------------------------------------------------------------------

def test_allowed_fixture_tree() -> None:
    root = FIXTURES_ROOT / "allowed"
    viols = _violations(root)
    if viols:
        fail(
            "allowed/ fixture tree produced violations:\n  "
            + "\n  ".join(viols)
        )
    ok("allowed/ fixture tree is violation-free")


def test_forbidden_fixture_tree() -> None:
    root = FIXTURES_ROOT / "forbidden"
    viols = _violations(root)
    if not viols:
        fail("forbidden/ fixture tree produced no violations — expected at least one per fixture")

    # Verify specific patterns are caught
    joined = "\n".join(viols)
    expected_patterns = [
        "[common] forbidden include of [core]",
        "[core] forbidden include of [serialization]",
        "[media] forbidden include of [communication]",
        "[serialization] forbidden include of [communication]",
        "[api] forbidden include of [cli]",
        "[communication] forbidden include of [subtitles]",
        "[communication] forbidden include of [media]",
        "[cli] forbidden include of [communication]",
        "[cli] forbidden include of [serialization]",
        "private header",
    ]
    for pattern in expected_patterns:
        if pattern not in joined:
            fail(
                f"Expected forbidden/ tree to contain violation matching {pattern!r}.\n"
                f"Actual violations:\n  " + "\n  ".join(viols)
            )
    ok(f"forbidden/ fixture tree caught {len(viols)} violation(s) with all expected patterns")


# ---------------------------------------------------------------------------
# Live project check
# ---------------------------------------------------------------------------

def test_project_tree_is_clean() -> None:
    viols = _violations(REPO_ROOT)
    if viols:
        fail(
            "Project tree has boundary violations:\n  "
            + "\n  ".join(viols)
        )
    ok("project tree is clean (no boundary violations)")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main() -> None:
    import tempfile

    print("Running module boundary tests...\n")

    test_module_of_file_recognition()

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        test_allowed_includes_inline(tmp_path / "allowed")
        test_forbidden_includes_inline(tmp_path / "forbidden")
        test_private_header_from_app_inline(tmp_path / "app_private")
        test_app_public_include_is_allowed_inline(tmp_path / "app_public")

    test_allowed_fixture_tree()
    test_forbidden_fixture_tree()
    test_project_tree_is_clean()

    print("\nAll module boundary tests passed.")


if __name__ == "__main__":
    main()
