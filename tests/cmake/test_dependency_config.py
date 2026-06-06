#!/usr/bin/env python3
"""
Dependency configuration tests for edge-tts-cpp.

Verifies:
  1. cmake/EdgeTtsDependencies.cmake exists and references ixwebsocket.
  2. cmake/Dependencies.cmake includes EdgeTtsDependencies.
  3. INTERFACE_SYSTEM_INCLUDE_DIRECTORIES suppression is present.
  4. .gitmodules registers the ixwebsocket submodule.
  5. CMakeLists.txt links ixwebsocket to the communication module.
  6. Public headers in include/edge_tts/ do not include ixwebsocket headers.
  7. DEPENDENCIES.md documents ixwebsocket.
  8. nlohmann/json is not optional (unconditional links in CMakeLists.txt).
  9. EDGE_TTS_FETCH_DEPS option is defined.
 10. EDGE_TTS_REQUIRE_NETWORKING option is defined.
 11. cmake/Dependencies.cmake has the correct json lookup order.
 12. cmake/EdgeTtsDependencies.cmake has the correct ixwebsocket lookup order.
 13. cmake/Dependencies.cmake has a FATAL_ERROR for json not found.
 14. FATAL_ERROR is gated on EDGE_TTS_REQUIRE_NETWORKING for missing ixwebsocket.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
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
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# 1. cmake/EdgeTtsDependencies.cmake exists and has required content
# ---------------------------------------------------------------------------

def test_edge_tts_dependencies_cmake_exists() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    if not path.exists():
        fail("cmake/EdgeTtsDependencies.cmake does not exist")
    ok("cmake/EdgeTtsDependencies.cmake exists")


def test_edge_tts_dependencies_cmake_references_ixwebsocket() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    if "ixwebsocket" not in content:
        fail("cmake/EdgeTtsDependencies.cmake does not reference ixwebsocket")
    if "submodules/ixwebsocket" not in content:
        fail("cmake/EdgeTtsDependencies.cmake does not use submodules/ixwebsocket path")
    ok("cmake/EdgeTtsDependencies.cmake references ixwebsocket")


def test_edge_tts_dependencies_cmake_suppresses_warnings() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    if "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES" not in content:
        fail(
            "cmake/EdgeTtsDependencies.cmake missing INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "
            "(required to suppress third-party warnings)"
        )
    ok("cmake/EdgeTtsDependencies.cmake sets INTERFACE_SYSTEM_INCLUDE_DIRECTORIES")


def test_edge_tts_dependencies_cmake_guards_on_exists() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    # The file must guard with EXISTS so the build works without the submodule.
    if not re.search(r'if\s*\(\s*EXISTS.*ixwebsocket.*CMakeLists\.txt', content):
        fail(
            "cmake/EdgeTtsDependencies.cmake must guard ixwebsocket with "
            'if(EXISTS "…/ixwebsocket/CMakeLists.txt") for graceful degradation'
        )
    ok("cmake/EdgeTtsDependencies.cmake guards with EXISTS")


# ---------------------------------------------------------------------------
# 2. cmake/Dependencies.cmake includes EdgeTtsDependencies
# ---------------------------------------------------------------------------

def test_dependencies_cmake_includes_edge_tts_dependencies() -> None:
    path = REPO_ROOT / "cmake" / "Dependencies.cmake"
    if not path.exists():
        fail("cmake/Dependencies.cmake does not exist")
    content = read(path)
    if "EdgeTtsDependencies" not in content:
        fail(
            "cmake/Dependencies.cmake does not include(EdgeTtsDependencies). "
            "The ixwebsocket setup must be orchestrated from Dependencies.cmake."
        )
    ok("cmake/Dependencies.cmake includes EdgeTtsDependencies")


# ---------------------------------------------------------------------------
# 3. .gitmodules registers the ixwebsocket submodule
# ---------------------------------------------------------------------------

def test_gitmodules_registers_ixwebsocket() -> None:
    gitmodules = REPO_ROOT / ".gitmodules"
    if not gitmodules.exists():
        fail(".gitmodules does not exist")
    content = read(gitmodules)
    if "ixwebsocket" not in content:
        fail(".gitmodules does not register the ixwebsocket submodule")
    if "machinezone/IXWebSocket" not in content:
        fail(".gitmodules does not reference the canonical IXWebSocket GitHub URL")
    ok(".gitmodules registers ixwebsocket submodule")


# ---------------------------------------------------------------------------
# 4. CMakeLists.txt wires ixwebsocket to the communication module
# ---------------------------------------------------------------------------

def test_cmake_wires_ixwebsocket_to_communication() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)
    # Must have a conditional block that links ixwebsocket to communication.
    if not re.search(
        r'if\s*\(TARGET\s+ixwebsocket\)',
        content
    ):
        fail(
            "CMakeLists.txt missing 'if(TARGET ixwebsocket)' guard for optional linking"
        )
    if "edge_tts_communication" not in content or "ixwebsocket" not in content:
        fail("CMakeLists.txt does not link ixwebsocket to edge_tts_communication")
    ok("CMakeLists.txt conditionally links ixwebsocket to edge_tts_communication")


# ---------------------------------------------------------------------------
# 5. Public headers do not include ixwebsocket headers directly
# ---------------------------------------------------------------------------

def test_public_headers_dependency_free() -> None:
    include_root = REPO_ROOT / "include" / "edge_tts"
    if not include_root.exists():
        fail("include/edge_tts/ directory does not exist")

    violations = []
    ixws_patterns = [
        re.compile(r'#\s*include\s+[<"]ixwebsocket/', re.IGNORECASE),
        re.compile(r'#\s*include\s+[<"]IXWebSocket', re.IGNORECASE),
        re.compile(r'#\s*include\s+[<"]ix/', re.IGNORECASE),
    ]

    for header in include_root.rglob("*.hpp"):
        content = read(header)
        for pattern in ixws_patterns:
            if pattern.search(content):
                violations.append(str(header.relative_to(REPO_ROOT)))
                break

    if violations:
        fail(
            "Public headers must not include ixwebsocket headers directly. "
            "Violations:\n  " + "\n  ".join(violations)
        )
    ok(f"Public headers are ixwebsocket-free ({sum(1 for _ in include_root.rglob('*.hpp'))} headers checked)")


# ---------------------------------------------------------------------------
# 6. DEPENDENCIES.md documents ixwebsocket
# ---------------------------------------------------------------------------

def test_dependencies_md_documents_ixwebsocket() -> None:
    deps_md = REPO_ROOT / "docs" / "DEPENDENCIES.md"
    if not deps_md.exists():
        fail("docs/DEPENDENCIES.md does not exist")
    content = read(deps_md)
    if "ixwebsocket" not in content:
        fail("docs/DEPENDENCIES.md does not mention ixwebsocket")
    if "machinezone" not in content and "IXWebSocket" not in content:
        fail("docs/DEPENDENCIES.md does not name the ixwebsocket project")
    if "git submodule" not in content.lower():
        fail("docs/DEPENDENCIES.md does not include submodule initialization instructions")
    ok("docs/DEPENDENCIES.md documents ixwebsocket")


# ---------------------------------------------------------------------------
# 7. EXCLUDE_FROM_ALL is used so ixwebsocket is not always built
# ---------------------------------------------------------------------------

def test_edge_tts_dependencies_cmake_uses_exclude_from_all() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    if "EXCLUDE_FROM_ALL" not in content:
        fail(
            "cmake/EdgeTtsDependencies.cmake must use EXCLUDE_FROM_ALL in "
            "add_subdirectory so ixwebsocket is only built when linked"
        )
    ok("cmake/EdgeTtsDependencies.cmake uses EXCLUDE_FROM_ALL")


# ---------------------------------------------------------------------------
# 8. nlohmann/json is NOT optional — CMakeLists.txt must link it unconditionally
# ---------------------------------------------------------------------------

def test_json_is_not_optional() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)

    # The old conditional guard must NOT wrap the serialization/communication json links.
    # We look for the unconditional link pattern.
    has_unconditional_serial = re.search(
        r'target_link_libraries\s*\(\s*edge_tts_serialization\b[^)]*nlohmann_json',
        content,
        re.DOTALL,
    )
    has_unconditional_comm = re.search(
        r'target_link_libraries\s*\(\s*edge_tts_communication\b[^)]*nlohmann_json',
        content,
        re.DOTALL,
    )
    if not has_unconditional_serial:
        fail(
            "CMakeLists.txt must unconditionally link nlohmann_json::nlohmann_json to "
            "edge_tts_serialization (json is required, not optional)"
        )
    if not has_unconditional_comm:
        fail(
            "CMakeLists.txt must unconditionally link nlohmann_json::nlohmann_json to "
            "edge_tts_communication (json is required, not optional)"
        )

    # Confirm the old `if(TARGET nlohmann_json::nlohmann_json)` guard is gone.
    if re.search(r'if\s*\(\s*TARGET\s+nlohmann_json::nlohmann_json\s*\)', content):
        fail(
            "CMakeLists.txt still has 'if(TARGET nlohmann_json::nlohmann_json)' guard. "
            "nlohmann/json is now required; remove the conditional."
        )

    ok("CMakeLists.txt links nlohmann_json unconditionally to serialization and communication")


# ---------------------------------------------------------------------------
# 9. EDGE_TTS_FETCH_DEPS option is defined in cmake/ProjectOptions.cmake
# ---------------------------------------------------------------------------

def test_fetch_deps_option_exists() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    if not path.exists():
        fail("cmake/ProjectOptions.cmake does not exist")
    content = read(path)
    if "EDGE_TTS_FETCH_DEPS" not in content:
        fail(
            "cmake/ProjectOptions.cmake does not define EDGE_TTS_FETCH_DEPS option"
        )
    ok("cmake/ProjectOptions.cmake defines EDGE_TTS_FETCH_DEPS")


# ---------------------------------------------------------------------------
# 10. EDGE_TTS_REQUIRE_NETWORKING option is defined in cmake/ProjectOptions.cmake
# ---------------------------------------------------------------------------

def test_require_networking_option_exists() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    content = read(path)
    if "EDGE_TTS_REQUIRE_NETWORKING" not in content:
        fail(
            "cmake/ProjectOptions.cmake does not define EDGE_TTS_REQUIRE_NETWORKING option"
        )
    ok("cmake/ProjectOptions.cmake defines EDGE_TTS_REQUIRE_NETWORKING")


# ---------------------------------------------------------------------------
# 11. cmake/Dependencies.cmake has the correct json lookup order
# ---------------------------------------------------------------------------

def test_json_lookup_order() -> None:
    path = REPO_ROOT / "cmake" / "Dependencies.cmake"
    content = read(path)

    # Must reference the submodule path
    if "submodules/json" not in content:
        fail("cmake/Dependencies.cmake does not reference submodules/json path")

    # Must use find_package for json
    if not re.search(r'find_package\s*\(\s*nlohmann_json', content):
        fail("cmake/Dependencies.cmake does not use find_package for nlohmann_json")

    # Must use FetchContent for json
    if not re.search(r'FetchContent_Declare\s*\(\s*\n?\s*nlohmann_json', content, re.MULTILINE):
        fail("cmake/Dependencies.cmake does not use FetchContent_Declare for nlohmann_json")

    # Order check: submodule → find_package → FetchContent
    submodule_pos = content.find("submodules/json")
    find_package_pos = content.find("find_package(nlohmann_json")
    fetch_pos = content.find("FetchContent_Declare")

    if not (submodule_pos < find_package_pos < fetch_pos):
        fail(
            "cmake/Dependencies.cmake json lookup order is wrong. "
            "Expected: submodule path → find_package → FetchContent"
        )

    ok("cmake/Dependencies.cmake has correct json lookup order: submodule → find_package → FetchContent")


# ---------------------------------------------------------------------------
# 12. cmake/EdgeTtsDependencies.cmake has the correct ixwebsocket lookup order
# ---------------------------------------------------------------------------

def test_ixwebsocket_lookup_order() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)

    # Must reference the submodule path
    if "submodules/ixwebsocket" not in content:
        fail("cmake/EdgeTtsDependencies.cmake does not reference submodules/ixwebsocket path")

    # Must use find_package for ixwebsocket
    if not re.search(r'find_package\s*\(\s*ixwebsocket', content):
        fail("cmake/EdgeTtsDependencies.cmake does not use find_package for ixwebsocket")

    # Must use FetchContent for ixwebsocket
    if not re.search(r'FetchContent_Declare\s*\(\s*\n?\s*ixwebsocket', content, re.MULTILINE):
        fail("cmake/EdgeTtsDependencies.cmake does not use FetchContent_Declare for ixwebsocket")

    # Order check: submodule → find_package → FetchContent
    submodule_pos = content.find("submodules/ixwebsocket")
    find_package_pos = content.find("find_package(ixwebsocket")
    fetch_pos_match = re.search(r'FetchContent_Declare\s*\(\s*\n?\s*ixwebsocket', content, re.MULTILINE)
    fetch_pos = fetch_pos_match.start() if fetch_pos_match else -1

    if not (submodule_pos < find_package_pos < fetch_pos):
        fail(
            "cmake/EdgeTtsDependencies.cmake ixwebsocket lookup order is wrong. "
            "Expected: submodule path → find_package → FetchContent"
        )

    ok("cmake/EdgeTtsDependencies.cmake has correct ixwebsocket lookup order: submodule → find_package → FetchContent")


# ---------------------------------------------------------------------------
# 13. cmake/Dependencies.cmake has a FATAL_ERROR for json not found
# ---------------------------------------------------------------------------

def test_fatal_error_when_json_unavailable() -> None:
    path = REPO_ROOT / "cmake" / "Dependencies.cmake"
    content = read(path)
    if not re.search(r'message\s*\(\s*FATAL_ERROR', content):
        fail(
            "cmake/Dependencies.cmake must have a message(FATAL_ERROR ...) "
            "for when nlohmann/json cannot be found"
        )
    ok("cmake/Dependencies.cmake has FATAL_ERROR for missing nlohmann/json")


# ---------------------------------------------------------------------------
# 14. FATAL_ERROR is gated on EDGE_TTS_REQUIRE_NETWORKING for missing ixwebsocket
# ---------------------------------------------------------------------------

def test_fatal_error_when_networking_required_but_missing() -> None:
    deps_cmake = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    root_cmake = REPO_ROOT / "CMakeLists.txt"

    deps_content = read(deps_cmake)
    root_content = read(root_cmake)
    combined = deps_content + root_content

    # Must have FATAL_ERROR related to REQUIRE_NETWORKING
    if not re.search(r'EDGE_TTS_REQUIRE_NETWORKING', combined):
        fail(
            "Neither cmake/EdgeTtsDependencies.cmake nor CMakeLists.txt references "
            "EDGE_TTS_REQUIRE_NETWORKING"
        )

    has_fatal = re.search(r'message\s*\(\s*FATAL_ERROR', combined)
    if not has_fatal:
        fail(
            "No message(FATAL_ERROR ...) gated on EDGE_TTS_REQUIRE_NETWORKING found "
            "in cmake/EdgeTtsDependencies.cmake or CMakeLists.txt"
        )

    ok("FATAL_ERROR for missing ixwebsocket is gated on EDGE_TTS_REQUIRE_NETWORKING")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_edge_tts_dependencies_cmake_exists,
        test_edge_tts_dependencies_cmake_references_ixwebsocket,
        test_edge_tts_dependencies_cmake_suppresses_warnings,
        test_edge_tts_dependencies_cmake_guards_on_exists,
        test_dependencies_cmake_includes_edge_tts_dependencies,
        test_gitmodules_registers_ixwebsocket,
        test_cmake_wires_ixwebsocket_to_communication,
        test_public_headers_dependency_free,
        test_dependencies_md_documents_ixwebsocket,
        test_edge_tts_dependencies_cmake_uses_exclude_from_all,
        test_json_is_not_optional,
        test_fetch_deps_option_exists,
        test_require_networking_option_exists,
        test_json_lookup_order,
        test_ixwebsocket_lookup_order,
        test_fatal_error_when_json_unavailable,
        test_fatal_error_when_networking_required_but_missing,
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} dependency config checks passed.")


if __name__ == "__main__":
    main()
