#!/usr/bin/env python3
"""
Consumer strict-warnings integration test.

Verifies two independent properties of the edge_tts::tts public target:

  1. No warning-flag leakage — applying -Wall -Wextra -Wpedantic -Werror on the
     consumer's own target must not cause build failures triggered by
     edge-tts-cpp internal includes (e.g. ixwebsocket headers visible as
     non-SYSTEM includes) or leaked compile definitions.

  2. No manual internal-dep linking — the consumer links ONLY edge_tts::tts.
     It does NOT list nlohmann_json, ixwebsocket, or any edge_tts_* sub-target.
     The build must still succeed (all transitive deps are satisfied
     automatically by the edge_tts::tts target's usage requirements).

Tests run in two modes:

  a) add_subdirectory mode — edge-tts-cpp is consumed from the source tree.
  b) find_package mode    — edge-tts-cpp is installed to a temp prefix first,
                            then the consumer uses find_package(edge_tts_cpp).

Requires cmake ≥ 3.24 on PATH; skipped otherwise.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FIXTURE_DIR = pathlib.Path(__file__).resolve().parent / "consumer_strict_warnings"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def run(cmd: list[str], timeout: int = 300) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def cmake_bin() -> str | None:
    return shutil.which("cmake")


# ---------------------------------------------------------------------------
# Mode A: add_subdirectory
# ---------------------------------------------------------------------------

def test_add_subdirectory_strict_warnings(tmp: pathlib.Path) -> None:
    build_dir = tmp / "build_subdir"

    configure_cmd = [
        cmake_bin(),
        "-S", str(FIXTURE_DIR),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        f"-DEDGE_TTS_CPP_SOURCE_DIR={REPO_ROOT}",
        "-DEDGE_TTS_BUILD_TESTS=OFF",
        "-DEDGE_TTS_BUILD_APPS=OFF",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            "add_subdirectory + strict warnings: configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("add_subdirectory + strict warnings: configured successfully")

    build_cmd = [cmake_bin(), "--build", str(build_dir), "--target", "consumer_strict_app"]
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            "add_subdirectory + strict warnings: build failed.\n"
            "This indicates that edge-tts-cpp leaked warning flags or internal "
            "headers (e.g. ixwebsocket) into the consumer's compile environment.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("add_subdirectory + strict warnings: built successfully — no flag leakage detected")


# ---------------------------------------------------------------------------
# Mode B: find_package (install first, then consume)
# ---------------------------------------------------------------------------

def install_edge_tts(tmp: pathlib.Path) -> pathlib.Path:
    """Configure, build and install edge-tts-cpp. Returns the install prefix."""
    build_dir = tmp / "lib_build"
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
            "find_package mode: configure of edge-tts-cpp failed.\n"
            f"stdout:\n{result.stdout[:2000]}\nstderr:\n{result.stderr[:2000]}"
        )

    build_targets = [
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
    ]
    build_cmd = [cmake_bin(), "--build", str(build_dir), "--target"] + build_targets
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            "find_package mode: build of edge-tts-cpp failed.\n"
            f"stdout:\n{result.stdout[:2000]}\nstderr:\n{result.stderr[:2000]}"
        )

    install_cmd = [cmake_bin(), "--install", str(build_dir), "--prefix", str(install_prefix)]
    result = run(install_cmd)
    if result.returncode != 0:
        fail(
            "find_package mode: cmake --install failed.\n"
            f"stdout:\n{result.stdout[:2000]}\nstderr:\n{result.stderr[:2000]}"
        )

    ok("find_package mode: edge-tts-cpp installed successfully")
    return install_prefix


def test_find_package_strict_warnings(tmp: pathlib.Path, install_prefix: pathlib.Path) -> None:
    build_dir = tmp / "build_findpkg"

    configure_cmd = [
        cmake_bin(),
        "-S", str(FIXTURE_DIR),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        f"-DCMAKE_PREFIX_PATH={install_prefix}",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            "find_package + strict warnings: configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("find_package + strict warnings: configured successfully")

    build_cmd = [cmake_bin(), "--build", str(build_dir), "--target", "consumer_strict_app"]
    result = run(build_cmd, timeout=300)
    if result.returncode != 0:
        fail(
            "find_package + strict warnings: build failed.\n"
            "This indicates that edge-tts-cpp leaked warning flags or internal "
            "headers into the consumer's compile environment via the installed package.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("find_package + strict warnings: built successfully — no flag leakage in installed package")


# ---------------------------------------------------------------------------
# Verification: installed CMake files do not contain build-tree paths
# ---------------------------------------------------------------------------

def test_no_build_tree_paths_in_install(
    install_prefix: pathlib.Path,
    build_dir: pathlib.Path,
) -> None:
    """Check that installed CMake config files contain no build-tree absolute paths."""
    cmake_dir = install_prefix / "lib" / "cmake" / "edge_tts_cpp"
    build_path_str = str(build_dir)

    leaked: list[str] = []
    for cmake_file in cmake_dir.iterdir():
        if cmake_file.suffix != ".cmake":
            continue
        content = cmake_file.read_text(encoding="utf-8")
        if build_path_str in content:
            leaked.append(cmake_file.name)

    if leaked:
        fail(
            f"Build-tree path '{build_path_str}' found in installed CMake file(s): "
            + ", ".join(leaked)
            + "\nInstalled targets must use _IMPORT_PREFIX-relative paths only."
        )
    ok("No build-tree paths in installed CMake files")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if not cmake_bin():
        print("  SKIP consumer strict-warnings test (cmake not on PATH)")
        sys.exit(0)

    if not FIXTURE_DIR.exists():
        fail(f"consumer_strict_warnings fixture not found: {FIXTURE_DIR}")

    with tempfile.TemporaryDirectory(prefix="edge_tts_strict_warn_") as tmp_str:
        tmp = pathlib.Path(tmp_str)

        # Mode A: add_subdirectory with strict warnings.
        test_add_subdirectory_strict_warnings(tmp)

        # Mode B: find_package with strict warnings.
        install_prefix = install_edge_tts(tmp)
        test_find_package_strict_warnings(tmp, install_prefix)

        # Bonus: verify no build-tree paths leaked into the install tree.
        lib_build_dir = tmp / "lib_build"
        test_no_build_tree_paths_in_install(install_prefix, lib_build_dir)

    print("\nAll consumer strict-warnings checks passed.")


if __name__ == "__main__":
    main()
