#!/usr/bin/env python3
"""
Package versioning and compatibility tests for edge-tts-cpp.

Verifies that:

  A. Version macros in version.hpp (EDGE_TTS_CPP_VERSION_*)
       1. version.hpp is present in the installed tree.
       2. The macros are numeric and non-negative.
       3. The macros match the CMake package version declared in
          edge_tts_cpp-config-version.cmake.
       4. A .cpp that static_asserts EDGE_TTS_CPP_VERSION_* values compiles.

  B. find_package version compatibility (CMake SameMajorVersion)
       5. find_package with no version constraint succeeds.
       6. find_package requesting the exact installed version succeeds.
       7. find_package requesting a compatible version (same major, lower minor)
          succeeds.
       8. find_package requesting an incompatible major version fails.
       9. find_package requesting a higher minor (REQUIRED, EXACT) fails.
      10. find_package requesting an exact mismatch (different patch) fails.

  C. Version string consistency
      11. README.md mentions the current project version string.

Steps:
  1. Install edge-tts-cpp to a temporary prefix.
  2. Read installed version from edge_tts_cpp-config-version.cmake.
  3. Run checks A and B using that prefix and version.
  4. Run check C against the repo README.

Requires cmake >= 3.24 and a C++20-capable CXX compiler on PATH.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def run(cmd: list[str], cwd: pathlib.Path | None = None,
        timeout: int = 300) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True,
                          timeout=timeout, cwd=cwd)


def cmake_bin() -> str | None:
    return shutil.which("cmake")


def find_cxx_compiler() -> str | None:
    for name in ("c++", "g++", "clang++"):
        path = shutil.which(name)
        if path:
            return path
    return None


# ---------------------------------------------------------------------------
# Install step (shared with other cmake tests)
# ---------------------------------------------------------------------------

def install_edge_tts(tmp: pathlib.Path) -> pathlib.Path:
    """Configure, build library targets, and install. Returns install prefix."""
    build_dir = tmp / "build"
    install_prefix = tmp / "install"

    configure_cmd = [
        cmake_bin(),
        "-S", str(REPO_ROOT),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        "-DEDGE_TTS_BUILD_APPS=OFF",
        "-DEDGE_TTS_BUILD_TESTS=OFF",
        "-DEDGE_TTS_INSTALL=ON",
        f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            f"Configure failed.\nstdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    build_cmd = [
        cmake_bin(), "--build", str(build_dir), "--target",
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
    ]
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            f"Build failed.\nstdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    install_cmd = [cmake_bin(), "--install", str(build_dir),
                   "--prefix", str(install_prefix)]
    result = run(install_cmd)
    if result.returncode != 0:
        fail(
            f"Install failed.\nstdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok(f"Installed edge-tts-cpp to {install_prefix}")
    return install_prefix


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def read_installed_version(install_prefix: pathlib.Path) -> str:
    """
    Extract PACKAGE_VERSION from the installed config-version.cmake.
    Returns e.g. '0.1.0'.
    """
    version_file = (
        install_prefix / "lib" / "cmake" / "edge_tts_cpp"
        / "edge_tts_cpp-config-version.cmake"
    )
    if not version_file.exists():
        fail(f"Config-version file not found: {version_file}")

    content = version_file.read_text(encoding="utf-8")
    m = re.search(r'set\s*\(\s*PACKAGE_VERSION\s+"([^"]+)"\s*\)', content)
    if not m:
        fail(
            f"Cannot parse PACKAGE_VERSION from {version_file}.\n"
            f"Content:\n{content[:500]}"
        )
    return m.group(1)


def split_version(v: str) -> tuple[int, int, int]:
    """Parse 'MAJOR.MINOR.PATCH' into (major, minor, patch)."""
    parts = v.split(".")
    if len(parts) != 3:
        fail(f"Version string '{v}' is not MAJOR.MINOR.PATCH")
    try:
        return int(parts[0]), int(parts[1]), int(parts[2])
    except ValueError as e:
        fail(f"Non-integer component in version '{v}': {e}")


def cmake_find_package(
    tmp_dir: pathlib.Path,
    install_prefix: pathlib.Path,
    version_args: str,
    label: str,
    expect_success: bool,
) -> None:
    """
    Configure a one-file CMakeLists.txt that calls find_package with version_args.
    Asserts that cmake exits with the expected success/failure.
    """
    build_dir = tmp_dir / f"fp_{label}"
    build_dir.mkdir(parents=True, exist_ok=True)

    cml = build_dir / "CMakeLists.txt"
    # LANGUAGES CXX is required: with LANGUAGES NONE, CMake's FindZLIB (called
    # by the edge_tts_cpp config file for ixwebsocket) cannot locate ZLIB_LIBRARY
    # because find_library() needs compiler-initialized search paths.
    cml.write_text(
        "cmake_minimum_required(VERSION 3.24)\n"
        "project(fp_test LANGUAGES CXX)\n"
        f"find_package(edge_tts_cpp {version_args} CONFIG REQUIRED)\n",
        encoding="utf-8",
    )

    result = run(
        [cmake_bin(), str(build_dir),
         f"-DCMAKE_PREFIX_PATH={install_prefix}",
         "-G", "Unix Makefiles"],
        cwd=build_dir,
    )

    if expect_success:
        if result.returncode != 0:
            fail(
                f"[{label}] find_package({version_args}) should succeed but failed.\n"
                f"stderr:\n{result.stderr[:2000]}"
            )
        ok(f"find_package({version_args}) succeeded (expected)")
    else:
        if result.returncode == 0:
            fail(
                f"[{label}] find_package({version_args}) should fail but succeeded.\n"
                "The installed package incorrectly reports compatibility with "
                f"version constraint '{version_args}'."
            )
        ok(f"find_package({version_args}) failed (expected — version incompatible)")


# ---------------------------------------------------------------------------
# Test A: version.hpp macros
# ---------------------------------------------------------------------------

def test_version_hpp_present(install_prefix: pathlib.Path) -> None:
    hpp = install_prefix / "include" / "edge_tts" / "version.hpp"
    if not hpp.exists():
        fail(f"version.hpp not found in install tree: {hpp}")
    ok("include/edge_tts/version.hpp present in install tree")


def test_version_hpp_content(install_prefix: pathlib.Path, version: str) -> None:
    """Check pragma once, macro definitions, and expected content."""
    hpp = install_prefix / "include" / "edge_tts" / "version.hpp"
    content = hpp.read_text(encoding="utf-8")

    if "#pragma once" not in content:
        fail("version.hpp missing #pragma once")

    major, minor, patch = split_version(version)
    for macro, expected in [
        ("EDGE_TTS_CPP_VERSION_MAJOR", str(major)),
        ("EDGE_TTS_CPP_VERSION_MINOR", str(minor)),
        ("EDGE_TTS_CPP_VERSION_PATCH", str(patch)),
        ("EDGE_TTS_CPP_VERSION",       f'"{version}"'),
    ]:
        if macro not in content:
            fail(f"version.hpp does not define {macro}")
        # Check the value is present somewhere after the macro name.
        if expected not in content:
            fail(
                f"version.hpp defines {macro} but expected value "
                f"'{expected}' not found in file."
            )

    # Check that constexpr values are declared inside namespace edge_tts.
    if "namespace edge_tts" not in content:
        fail("version.hpp does not open namespace edge_tts")
    if "version_major" not in content or "version_minor" not in content:
        fail("version.hpp does not declare version_major/version_minor constexpr")
    ok(f"version.hpp content is correct for version {version}")


def test_version_hpp_compiles(
    install_prefix: pathlib.Path,
    version: str,
    cxx: str,
    tmp: pathlib.Path,
) -> None:
    """
    Compile a .cpp that static_asserts the version macros equal the installed
    package version.  Compilation failure means the macros are wrong or the
    header is broken.
    """
    major, minor, patch = split_version(version)
    src = tmp / "version_assert.cpp"
    src.write_text(
        "#include <edge_tts/version.hpp>\n"
        f"static_assert(EDGE_TTS_CPP_VERSION_MAJOR == {major},\n"
        f'    "EDGE_TTS_CPP_VERSION_MAJOR mismatch");\n'
        f"static_assert(EDGE_TTS_CPP_VERSION_MINOR == {minor},\n"
        f'    "EDGE_TTS_CPP_VERSION_MINOR mismatch");\n'
        f"static_assert(EDGE_TTS_CPP_VERSION_PATCH == {patch},\n"
        f'    "EDGE_TTS_CPP_VERSION_PATCH mismatch");\n'
        # Verify version constexpr values match the macros.
        "static_assert(edge_tts::version_major == EDGE_TTS_CPP_VERSION_MAJOR);\n"
        "static_assert(edge_tts::version_minor == EDGE_TTS_CPP_VERSION_MINOR);\n"
        "static_assert(edge_tts::version_patch == EDGE_TTS_CPP_VERSION_PATCH);\n",
        encoding="utf-8",
    )

    include_dir = install_prefix / "include"
    result = run([
        cxx,
        "-std=c++20",
        f"-I{include_dir}",
        "-fsyntax-only",
        str(src),
    ])
    if result.returncode != 0:
        fail(
            "version.hpp static_assert compilation failed:\n"
            + (result.stdout + result.stderr).strip()[:2000]
        )
    ok("version.hpp static_assert compilation passed")


# ---------------------------------------------------------------------------
# Test B: find_package version compatibility
# ---------------------------------------------------------------------------

def test_find_package_versions(
    install_prefix: pathlib.Path,
    version: str,
    tmp: pathlib.Path,
) -> None:
    major, minor, patch = split_version(version)

    # 5. No version constraint — always succeeds.
    cmake_find_package(tmp, install_prefix, "", "no_version", True)

    # 6. Exact installed version — succeeds.
    cmake_find_package(
        tmp, install_prefix,
        f"{version} EXACT", "exact_match", True,
    )

    # 7. Compatible version — same major, lower minor (if minor > 0).
    if minor > 0:
        compat_version = f"{major}.0"
        cmake_find_package(
            tmp, install_prefix,
            compat_version, "compat_lower_minor", True,
        )
    elif patch > 0:
        compat_version = f"{major}.{minor}.0"
        cmake_find_package(
            tmp, install_prefix,
            compat_version, "compat_lower_patch", True,
        )
    else:
        ok("find_package compat_lower (skipped — version is x.0.0)")

    # 8. Incompatible major — fails.
    incompatible_major = f"{major + 1}.0"
    cmake_find_package(
        tmp, install_prefix,
        incompatible_major, "incompatible_major", False,
    )

    # 9. Higher minor with EXACT — fails.
    higher_minor_exact = f"{major}.{minor + 1}.0 EXACT"
    cmake_find_package(
        tmp, install_prefix,
        higher_minor_exact, "higher_minor_exact", False,
    )

    # 10. Different patch EXACT — fails (only when patch != 999).
    if patch != 999:
        diff_patch_exact = f"{major}.{minor}.{patch + 1} EXACT"
        cmake_find_package(
            tmp, install_prefix,
            diff_patch_exact, "diff_patch_exact", False,
        )
    else:
        ok("find_package diff_patch_exact (skipped — patch is 999)")


# ---------------------------------------------------------------------------
# Test C: README version consistency
# ---------------------------------------------------------------------------

def test_readme_mentions_version(version: str) -> None:
    """
    README.md must mention the current project version string so that users
    reading the README can confirm which version they are looking at.
    """
    readme = REPO_ROOT / "README.md"
    if not readme.exists():
        fail("README.md not found")
    content = readme.read_text(encoding="utf-8")
    if version not in content:
        fail(
            f"README.md does not mention the current project version '{version}'.\n"
            f"Add a version badge or version line to the README so consumers know "
            f"which version the documentation describes."
        )
    ok(f"README.md mentions current version '{version}'")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if not cmake_bin():
        print("  SKIP package version tests (cmake not on PATH)")
        sys.exit(0)

    cxx = find_cxx_compiler()
    if not cxx:
        print("  SKIP package version tests (no C++ compiler on PATH)")
        sys.exit(0)

    if platform.system() == "Windows":
        print("  SKIP package version tests (Windows -fsyntax-only not yet supported)")
        sys.exit(0)

    with tempfile.TemporaryDirectory(prefix="edge_tts_ver_") as tmp_str:
        tmp = pathlib.Path(tmp_str)

        install_prefix = install_edge_tts(tmp)
        version = read_installed_version(install_prefix)
        ok(f"Installed version: {version}")

        # A: version.hpp checks
        test_version_hpp_present(install_prefix)
        test_version_hpp_content(install_prefix, version)
        test_version_hpp_compiles(install_prefix, version, cxx, tmp)

        # B: find_package version compatibility
        test_find_package_versions(install_prefix, version, tmp)

        # C: README consistency
        test_readme_mentions_version(version)

    print("\nAll package version checks passed.")


if __name__ == "__main__":
    main()
