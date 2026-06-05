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
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} dependency config checks passed.")


if __name__ == "__main__":
    main()
