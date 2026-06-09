#!/usr/bin/env python3
"""
Repository hygiene tests for edge-tts-cpp.

Prevents regression into skeleton behavior, accidental fake-client usage in
production code, checked-in build artifacts, or stale skeleton placeholders.

Checks:
  1. No skeleton placeholder strings in production source.
  2. No fake client headers included from non-fake production files.
  3. No removed skeleton files still tracked in git.
  4. No build artifacts tracked in git.
  5. Docs/CONTRIBUTING.md documents hygiene rules.

Production source = modules/*/src/, modules/*/include/*/, apps/
Tests and docs are NOT scanned for placeholder strings (docs may legitimately
explain historical cleanup; tests may include Fake* headers by design).

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _strip_comments(content: str) -> str:
    """Remove C/C++ line and block comments so patterns in comments are ignored."""
    content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
    content = re.sub(r"//[^\n]*", "", content)
    return content


def _production_source_files() -> list[pathlib.Path]:
    """
    Return all .cpp/.hpp files from production source directories:
      modules/*/src/  modules/*/include/*/  apps/
    Also includes the top-level include/edge_tts/ (umbrella header).
    Excludes third-party submodules and test fixture trees.
    """
    result: list[pathlib.Path] = []
    modules_dir = REPO_ROOT / "modules"
    if modules_dir.exists():
        for mod_dir in sorted(modules_dir.iterdir()):
            if not mod_dir.is_dir():
                continue
            for subdir in ("src", "include"):
                d = mod_dir / subdir
                if d.exists():
                    for ext in ("*.cpp", "*.hpp"):
                        result.extend(d.rglob(ext))
    for d in [REPO_ROOT / "include" / "edge_tts", REPO_ROOT / "apps"]:
        if d.exists():
            for ext in ("*.cpp", "*.hpp"):
                result.extend(d.rglob(ext))
    return sorted(result)


# ---------------------------------------------------------------------------
# 1. No skeleton placeholder strings in production source
#
# These strings are remnants of the original skeleton and must never re-appear
# in production code under src/, include/edge_tts/, or apps/.
# Docs (docs/) are NOT scanned — historical notes may reference these strings.
# ---------------------------------------------------------------------------

SKELETON_PATTERNS: list[tuple[str, str]] = [
    # (pattern, human-readable description)
    ("WebSocket transport is not implemented yet",
     "skeleton WebSocketTransport stub message"),
    ("TODO_TRUSTED_CLIENT_TOKEN",
     "skeleton EdgeToken placeholder token"),
    ("TODO_CONNECTION_ID",
     "skeleton EdgeToken placeholder connection ID"),
    ("TODO_SEC_MS_GEC",
     "skeleton EdgeToken placeholder DRM token"),
    # Generic patterns that indicate unfinished work in production code.
    # These should never appear in src/, include/edge_tts/, or apps/.
    ("vibe code",
     "vibe-code marker in production source"),
    ("Task 9:",
     "scaffolding task reference in production source"),
    ("pending implementation",
     "pending-implementation marker in production source"),
]


def test_no_skeleton_placeholders_in_production() -> None:
    violations: list[str] = []

    for path in _production_source_files():
        code = _strip_comments(read(path))
        rel = path.relative_to(REPO_ROOT)
        for pattern, desc in SKELETON_PATTERNS:
            if pattern in code:
                # Find the line number for a useful error message.
                for lineno, line in enumerate(read(path).splitlines(), 1):
                    stripped = _strip_comments(line)
                    if pattern in stripped:
                        violations.append(
                            f"{rel}:{lineno}: found skeleton placeholder "
                            f"'{pattern}' ({desc})"
                        )

    if violations:
        fail(
            "Production source contains skeleton placeholder strings.\n"
            "These are stale skeleton remnants that should have been removed "
            "when the real implementation was added.\n\n"
            "Violations:\n  " + "\n  ".join(violations)
        )
    ok("No skeleton placeholder strings found in production source")


# ---------------------------------------------------------------------------
# 2. No fake client headers included from non-fake production files
#
# FakeHttpClient and FakeWebSocketClient are test doubles.  They must only be:
#   - Defined in their own Fake*.hpp / Fake*.cpp files (the canonical location)
#   - Included from test files (tests/**) or from their own Fake*.cpp file
#
# Production non-Fake source files (src/**/*.cpp, src/**/*.hpp,
# include/edge_tts/**/*.hpp, apps/**/*) must never #include a Fake*.hpp header.
# ---------------------------------------------------------------------------

FAKE_CLIENT_HEADERS = (
    "FakeHttpClient.hpp",
    "FakeWebSocketClient.hpp",
    "FakeProcessRunner.hpp",
)

FAKE_INCLUDE_PATTERN = re.compile(
    r'#\s*include\s+"[^"]*(' + "|".join(re.escape(h) for h in FAKE_CLIENT_HEADERS) + r')"'
)


def test_no_fake_client_includes_in_production() -> None:
    violations: list[str] = []

    for path in _production_source_files():
        # The fake client definition files themselves are allowed to
        # include one another (e.g. FakeHttpClient.hpp includes common headers).
        if path.name.startswith("Fake"):
            continue

        content = read(path)
        rel = path.relative_to(REPO_ROOT)
        for lineno, line in enumerate(content.splitlines(), 1):
            if FAKE_INCLUDE_PATTERN.search(line):
                violations.append(
                    f"{rel}:{lineno}: production file includes a fake "
                    f"client header — {line.strip()!r}"
                )

    if violations:
        fail(
            "Production source files must not include Fake* test-double headers.\n"
            "Fake clients belong only in tests/ or inside their own Fake*.cpp "
            "definition files.\n\n"
            "Violations:\n  " + "\n  ".join(violations)
        )
    ok("No fake client headers included from production source files")


# ---------------------------------------------------------------------------
# 2b. No Fake* class definitions embedded in non-Fake production headers
#
# Fake classes must live in their own Fake*.hpp files, not be inlined into a
# production header like ProcessRunner.hpp.  This check catches the pattern
# "class FakeXxx" appearing in a header whose name does not start with "Fake".
# ---------------------------------------------------------------------------

FAKE_CLASS_PATTERN = re.compile(r'\bclass\s+Fake[A-Z]')


def test_no_fake_class_defined_in_production_headers() -> None:
    violations: list[str] = []

    for path in _production_source_files():
        if not path.suffix == ".hpp":
            continue
        if path.name.startswith("Fake"):
            continue

        content = _strip_comments(read(path))
        rel = path.relative_to(REPO_ROOT)
        for lineno, line in enumerate(read(path).splitlines(), 1):
            stripped = _strip_comments(line)
            if FAKE_CLASS_PATTERN.search(stripped):
                violations.append(
                    f"{rel}:{lineno}: Fake class defined in non-Fake header — {line.strip()!r}"
                )

    if violations:
        fail(
            "Fake test-double classes must be defined in their own Fake*.hpp files.\n"
            "Move the class declaration to include/edge_tts/<module>/Fake<Name>.hpp "
            "and include it from test files only.\n\n"
            "Violations:\n  " + "\n  ".join(violations)
        )
    ok("No Fake* class definitions found in non-Fake production headers")


# ---------------------------------------------------------------------------
# 3. Known removed skeleton files must not exist on disk
#
# These files were part of the original skeleton and have been superseded by
# real implementations.  Re-adding them would silently re-introduce skeleton
# behavior because they are still wired into legacy CMake targets.
# ---------------------------------------------------------------------------

REMOVED_SKELETON_FILES: list[str] = [
    # EdgeToken was the skeleton DRM token class.  Replaced by EdgeTokenProvider.
    "modules/serialization/src/EdgeToken.cpp",
    "modules/serialization/include/serialization/EdgeToken.hpp",
    # WebSocketTransport was the skeleton ITransport stub.  Replaced by WebSocketClient.
    "modules/communication/src/WebSocketTransport.cpp",
    "modules/communication/include/communication/WebSocketTransport.hpp",
    # Transport.hpp defined the ITransport / RawMessage skeleton interface.
    # The real interface is IWebSocketClient / WebSocketMessage.
    "modules/communication/include/communication/Transport.hpp",
    # serialization::EdgeProtocol was a skeleton frame-builder with hardcoded JSON and
    # no timestamp support.  Replaced by communication::EdgeProtocol (full implementation).
    "modules/serialization/src/EdgeProtocol.cpp",
    "modules/serialization/include/serialization/EdgeProtocol.hpp",
    # HttpVoiceService was a skeleton voice-list stub that returned an empty vector.
    # Replaced by VoiceService (DRM token injection, HTTP retry on 403).
    "modules/communication/src/HttpVoiceService.cpp",
    "modules/communication/include/communication/HttpVoiceService.hpp",
    # VoicesManager wrapped HttpVoiceService and is equally stale.
    # The VoiceFilter API on VoiceService replaces its find_by_locale().
    "modules/communication/src/VoicesManager.cpp",
    "modules/communication/include/communication/VoicesManager.hpp",
    # CliPlaceholder.cpp was the sole translation unit of the cli module before real
    # argument parsing was implemented.  CliOptions.hpp was the struct it included.
    # Both are superseded by EdgeTtsArguments / PlaybackArguments.
    "modules/cli/src/CliPlaceholder.cpp",
    "modules/cli/include/cli/CliOptions.hpp",
    # core::TextChunker was a raw UTF-8 byte splitter with no production callers.
    # All Edge-SSML-aware chunking lives in serialization::TextChunker, which
    # normalizes, XML-escapes, and applies the 4096-byte escaped-length limit.
    # Re-adding core::TextChunker would create the same ambiguity this task resolved.
    "modules/core/src/TextChunker.cpp",
    "modules/core/include/core/TextChunker.hpp",
]


def test_removed_skeleton_files_are_absent() -> None:
    present: list[str] = []
    for rel in REMOVED_SKELETON_FILES:
        path = REPO_ROOT / rel
        if path.exists():
            present.append(rel)

    if present:
        fail(
            "Removed skeleton files have re-appeared on disk.\n"
            "These files were superseded by real implementations and must "
            "not be re-created:\n  " + "\n  ".join(present)
        )
    ok("All removed skeleton files are absent from disk")


# ---------------------------------------------------------------------------
# 4. Build artifacts must not be tracked in git
#
# CMakeFiles/, build/, *.o, *.a, *.so, compiled binaries — none should ever
# be committed.  .gitignore already excludes them; this check catches if
# someone accidentally force-adds them.
# ---------------------------------------------------------------------------

BUILD_ARTIFACT_PATTERNS = re.compile(
    r"^(CMakeFiles/|build/|cmake-build-|out/|"
    r".*\.(o|a|so|dylib|dll|exe|bin|obj)$)"
)


def test_no_build_artifacts_in_git() -> None:
    try:
        result = subprocess.run(
            ["git", "ls-files"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        fail(f"Could not run 'git ls-files': {exc}")

    tracked = result.stdout.splitlines()
    artifacts: list[str] = []
    for f in tracked:
        if BUILD_ARTIFACT_PATTERNS.search(f):
            artifacts.append(f)

    if artifacts:
        fail(
            "Build artifacts are tracked in git.\n"
            "Add them to .gitignore and run 'git rm --cached' to remove:\n  "
            + "\n  ".join(artifacts)
        )
    ok("No build artifacts are tracked in git")


# ---------------------------------------------------------------------------
# 5. Skeleton source files must not be compiled (not in git ls-files)
#
# The CMakeLists.txt must not reference removed skeleton source files.
# We verify by checking that those paths do not appear in git-tracked files
# AND do not appear in CMakeLists.txt as source arguments.
# ---------------------------------------------------------------------------

def test_skeleton_source_not_in_cmake() -> None:
    cmake_path = REPO_ROOT / "CMakeLists.txt"
    if not cmake_path.exists():
        fail("CMakeLists.txt not found at repo root")
    cmake_text = _strip_comments(read(cmake_path))

    # Also check tests/CMakeLists.txt
    tests_cmake = REPO_ROOT / "tests" / "CMakeLists.txt"
    if tests_cmake.exists():
        cmake_text += _strip_comments(read(tests_cmake))

    violations: list[str] = []
    for rel in REMOVED_SKELETON_FILES:
        # Match on the full relative path so files with the same basename in
        # different modules (e.g. serialization/EdgeProtocol.cpp vs
        # communication/EdgeProtocol.cpp) do not produce false positives.
        if rel in cmake_text:
            violations.append(rel)

    if violations:
        fail(
            "Removed skeleton files are still referenced in CMakeLists.txt.\n"
            "Remove these entries so the skeleton code is not compiled:\n  "
            + "\n  ".join(violations)
        )
    ok("No removed skeleton files referenced in CMakeLists.txt")


# ---------------------------------------------------------------------------
# 6. Fake headers must not exist under include/edge_tts/ (public install tree)
#
# Fake implementations belong exclusively in tests/support/.  If a Fake*.hpp
# appears under include/edge_tts/ it would be exported as part of the public
# API and confuse downstream consumers.
# ---------------------------------------------------------------------------

FAKE_HEADER_NAMES = (
    "FakeHttpClient.hpp",
    "FakeWebSocketClient.hpp",
    "FakeProcessRunner.hpp",
)


def test_fake_headers_not_in_public_include() -> None:
    found: list[str] = []
    # Check top-level include/edge_tts/ (umbrella)
    public_include = REPO_ROOT / "include" / "edge_tts"
    if public_include.exists():
        for name in FAKE_HEADER_NAMES:
            for hit in public_include.rglob(name):
                found.append(str(hit.relative_to(REPO_ROOT)))
    # Check each module's include/ directory
    modules_dir = REPO_ROOT / "modules"
    if modules_dir.exists():
        for mod_dir in sorted(modules_dir.iterdir()):
            include_dir = mod_dir / "include"
            if include_dir.exists():
                for name in FAKE_HEADER_NAMES:
                    for hit in include_dir.rglob(name):
                        found.append(str(hit.relative_to(REPO_ROOT)))

    if found:
        fail(
            "Fake test-double headers found in module include/ trees (public API tree).\n"
            "Move them to tests/support/ and update test include "
            "directories accordingly:\n  " + "\n  ".join(found)
        )
    ok("No Fake* headers found in module include/ trees (public include tree)")


# ---------------------------------------------------------------------------
# 7. Fake source files must not exist under src/ (production source tree)
#
# Fake source files were moved to tests/support/.  Placing them back in src/
# would cause them to be compiled into production libraries.
# ---------------------------------------------------------------------------

FAKE_SOURCE_NAMES = (
    "FakeHttpClient.cpp",
    "FakeWebSocketClient.cpp",
    "FakeProcessRunner.cpp",
)


def test_fake_sources_not_in_production_src() -> None:
    found: list[str] = []
    modules_dir = REPO_ROOT / "modules"
    if modules_dir.exists():
        for mod_dir in sorted(modules_dir.iterdir()):
            src_dir = mod_dir / "src"
            if src_dir.exists():
                for name in FAKE_SOURCE_NAMES:
                    for hit in src_dir.rglob(name):
                        found.append(str(hit.relative_to(REPO_ROOT)))

    if found:
        fail(
            "Fake test-double source files found under modules/*/src/ (production source tree).\n"
            "These files must live in tests/support/ and be compiled only by the "
            "edge_tts_test_support CMake target:\n  " + "\n  ".join(found)
        )
    ok("No Fake* source files found in modules/*/src/ (production source tree)")


# ---------------------------------------------------------------------------
# 8. Root CMakeLists.txt production targets must not reference Fake*.cpp
#
# The production library targets (edge_tts_media, edge_tts_communication, etc.)
# must not list Fake*.cpp as source files.  Fake sources belong exclusively in
# the edge_tts_test_support target defined in tests/CMakeLists.txt.
# ---------------------------------------------------------------------------

def test_production_cmake_does_not_compile_fakes() -> None:
    cmake_path = REPO_ROOT / "CMakeLists.txt"
    if not cmake_path.exists():
        fail("CMakeLists.txt not found at repo root")

    content = _strip_comments(read(cmake_path))
    violations: list[str] = []
    for lineno, line in enumerate(content.splitlines(), 1):
        for name in FAKE_SOURCE_NAMES:
            if name in line:
                violations.append(f"CMakeLists.txt:{lineno}: {line.strip()!r}")

    if violations:
        fail(
            "Root CMakeLists.txt references Fake source files in production targets.\n"
            "Move Fake*.cpp to tests/support/ and add them only to the "
            "edge_tts_test_support target in tests/CMakeLists.txt:\n  "
            + "\n  ".join(violations)
        )
    ok("Root CMakeLists.txt production targets do not compile Fake*.cpp sources")


# ---------------------------------------------------------------------------
# 9. edge_tts_tts public target must not link cli, playback apps, or test-support
#
# edge_tts::tts is the stable consumer-facing target.  It must never pull in
# CLI argument-parsing, playback infrastructure, or test utilities into a
# normal TTS consumer link graph.
# ---------------------------------------------------------------------------

def test_edge_tts_tts_does_not_link_internal_targets() -> None:
    cmake_path = REPO_ROOT / "CMakeLists.txt"
    if not cmake_path.exists():
        fail("CMakeLists.txt not found at repo root")

    content = _strip_comments(read(cmake_path))

    # Find the target_link_libraries block for edge_tts_tts.
    match = re.search(
        r'target_link_libraries\s*\(\s*edge_tts_tts\b(.*?)\)',
        content,
        re.DOTALL,
    )
    if not match:
        fail(
            "CMakeLists.txt has no target_link_libraries(edge_tts_tts ...) call.\n"
            "edge_tts::tts must be defined and explicitly linked."
        )

    link_block = match.group(1)

    # These targets must never appear in edge_tts_tts's direct link spec.
    forbidden_fragments = [
        ("edge_tts_cli",     "CLI argument-parsing module"),
        ("edge_tts::cli",    "CLI argument-parsing module alias"),
        ("edge-tts",         "edge-tts app executable"),
        ("edge-playback",    "edge-playback app executable"),
        ("test_support",     "test-support target"),
    ]
    violations = []
    for fragment, description in forbidden_fragments:
        if fragment in link_block:
            violations.append(f"'{fragment}' ({description})")

    if violations:
        fail(
            "edge_tts_tts is directly linked to internal targets that must not "
            "be part of the public consumer API:\n  "
            + "\n  ".join(violations)
            + "\n\nedge_tts::tts must be minimal — only link edge_tts::api."
        )
    ok("edge_tts_tts does not link cli, playback apps, or test-support targets")


# ---------------------------------------------------------------------------
# 10. docs/CONTRIBUTING.md documents hygiene rules
# ---------------------------------------------------------------------------

CONTRIBUTING_REQUIRED_SECTIONS = [
    ("no fake clients", ["fake", "production"]),
    ("no build artifacts", ["build artifact", "git rm", ".gitignore"]),
    ("hygiene tests", ["test_repository_hygiene"]),
    ("TODO format", ["TODO"]),
]


def test_contributing_documents_hygiene() -> None:
    contributing = REPO_ROOT / "docs" / "CONTRIBUTING.md"
    if not contributing.exists():
        fail("docs/CONTRIBUTING.md does not exist")

    content = read(contributing).lower()
    missing: list[str] = []
    for section_name, keywords in CONTRIBUTING_REQUIRED_SECTIONS:
        if not any(kw.lower() in content for kw in keywords):
            missing.append(
                f"section on '{section_name}' (expected one of: "
                + ", ".join(repr(k) for k in keywords)
                + ")"
            )

    if missing:
        fail(
            "docs/CONTRIBUTING.md is missing hygiene guidance.\n"
            "Add the following sections:\n  " + "\n  ".join(missing)
        )
    ok("docs/CONTRIBUTING.md documents all required hygiene rules")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_no_skeleton_placeholders_in_production,
        test_no_fake_client_includes_in_production,
        test_no_fake_class_defined_in_production_headers,
        test_removed_skeleton_files_are_absent,
        test_no_build_artifacts_in_git,
        test_skeleton_source_not_in_cmake,
        test_fake_headers_not_in_public_include,
        test_fake_sources_not_in_production_src,
        test_production_cmake_does_not_compile_fakes,
        test_edge_tts_tts_does_not_link_internal_targets,
        test_contributing_documents_hygiene,
    ]
    print("Running repository hygiene tests...\n")
    for t in tests:
        t()
    print(f"\nAll {len(tests)} repository hygiene checks passed.")


if __name__ == "__main__":
    main()
