#!/usr/bin/env python3
"""
Regression check: edge-tts-cpp's own CMake files must not use bare
CMAKE_SOURCE_DIR or CMAKE_BINARY_DIR.

Both variables refer to the *top-level* project's source/binary directory.
When edge-tts-cpp is consumed via add_subdirectory() those variables point
to the *parent* project, breaking submodule paths, include paths, and test
script paths.

All project-local paths must use EDGE_TTS_SOURCE_DIR / EDGE_TTS_BINARY_DIR,
which are set to CMAKE_CURRENT_SOURCE_DIR / CMAKE_CURRENT_BINARY_DIR at the
top of edge-tts-cpp's own CMakeLists.txt.

Allowed exceptions (add to WHITELIST with a reason comment below):
  • Consumer fixture files under tests/cmake/consumer_add_subdirectory_basic/
    intentionally reference CMAKE_SOURCE_DIR to *verify* it is not corrupted.
  • This test script itself (contains the strings in comments/strings).

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent

# Directories excluded from the scan.
# Submodules are third-party projects that legitimately use CMAKE_SOURCE_DIR
# for their own build systems.  We must not mandate that they change.
# Consumer fixture directories are standalone projects that intentionally
# read CMAKE_SOURCE_DIR to verify it has not been corrupted.
EXCLUDED_DIRS = frozenset([
    REPO_ROOT / "submodules",
    REPO_ROOT / ".claude",
    REPO_ROOT / "tests" / "cmake" / "consumer_add_subdirectory_basic",
    REPO_ROOT / "tests" / "cmake" / "consumer_tts_target_basic",
    # consumer_install_basic is a standalone find_package consumer that may
    # legitimately use CMAKE_SOURCE_DIR (its own source root, not edge-tts-cpp's).
    REPO_ROOT / "tests" / "cmake" / "consumer_install_basic",
])

# Patterns that indicate a bare (project-corrupting) use of the variables.
FORBIDDEN = re.compile(r'\$\{CMAKE_SOURCE_DIR\}|\$\{CMAKE_BINARY_DIR\}')

# Patterns for lines that are the *definition* of the canonical variables;
# these are fine even though they appear in CMakeLists.txt.
ALLOWED_DEFINITION = re.compile(
    r'set\s*\(\s*EDGE_TTS_(SOURCE|BINARY)_DIR\b'
)


def is_excluded(path: pathlib.Path) -> bool:
    # Always exclude this script itself (it mentions the forbidden strings).
    if path == pathlib.Path(__file__).resolve():
        return True
    for excluded_dir in EXCLUDED_DIRS:
        try:
            path.relative_to(excluded_dir)
            return True
        except ValueError:
            pass
    return False


def scan_file(path: pathlib.Path) -> list[tuple[int, str]]:
    """Return list of (line_number, line_text) for violations."""
    violations = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return violations

    for i, line in enumerate(lines, start=1):
        if not FORBIDDEN.search(line):
            continue
        # Skip lines that are the canonical variable definition.
        if ALLOWED_DEFINITION.search(line):
            continue
        violations.append((i, line.rstrip()))
    return violations


def collect_cmake_files() -> list[pathlib.Path]:
    """Collect all CMake files that belong to edge-tts-cpp's own build system."""
    files = []

    # Root CMakeLists.txt
    root = REPO_ROOT / "CMakeLists.txt"
    if root.exists():
        files.append(root)

    # cmake/*.cmake
    for p in sorted((REPO_ROOT / "cmake").glob("*.cmake")):
        files.append(p)

    # tests/ CMakeLists.txt files (but not the consumer fixture sub-project)
    for p in sorted(REPO_ROOT.rglob("CMakeLists.txt")):
        if p == root:
            continue
        if is_excluded(p):
            continue
        files.append(p)

    # tests/ *.cmake helper files
    for p in sorted(REPO_ROOT.rglob("*.cmake")):
        if is_excluded(p):
            continue
        if p.parent == REPO_ROOT / "cmake":
            continue  # already added above
        files.append(p)

    return files


def main() -> None:
    files = collect_cmake_files()
    all_violations: list[tuple[pathlib.Path, int, str]] = []

    for path in files:
        if is_excluded(path):
            continue
        for lineno, line in scan_file(path):
            all_violations.append((path, lineno, line))

    if all_violations:
        print("FAIL: bare CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR found in edge-tts-cpp CMake files.")
        print()
        print("These variables refer to the top-level project's directory.")
        print("When edge-tts-cpp is consumed via add_subdirectory(), they point to the")
        print("parent project, breaking submodule paths, include dirs, and test scripts.")
        print()
        print("Replace with EDGE_TTS_SOURCE_DIR / EDGE_TTS_BINARY_DIR (defined in the")
        print("root CMakeLists.txt as CMAKE_CURRENT_SOURCE_DIR / CMAKE_CURRENT_BINARY_DIR).")
        print()
        for path, lineno, line in all_violations:
            rel = path.relative_to(REPO_ROOT)
            print(f"  {rel}:{lineno}: {line.strip()}")
        sys.exit(1)

    print(f"  OK  No bare CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR in {len(files)} CMake files.")


if __name__ == "__main__":
    main()
