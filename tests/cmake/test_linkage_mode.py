#!/usr/bin/env python3
"""
Linkage mode verification test for edge-tts-cpp.

edge-tts-cpp only supports STATIC library builds.  All edge_tts_* compiled
modules use an explicit STATIC keyword in add_library(), so BUILD_SHARED_LIBS
has no effect on the module type.

Steps:
  1. Verify EdgeTtsAddModule.cmake uses the STATIC keyword.
  2. Build all production modules with default settings; verify .a archives.
  3. Build with BUILD_SHARED_LIBS=ON; verify .a archives are still produced and
     no .so / .dylib files exist for edge_tts modules.
  4. Verify the consumer_static_build fixture links correctly (add_subdirectory).
  5. Install and verify the consumer_static_build fixture links correctly
     (find_package / install-tree mode).
  6. Verify docs/CONSUMING.md documents the static-only constraint.

Requires cmake >= 3.24 on PATH; the test is skipped otherwise.

Exit code 0 on success, non-zero on failure.
"""

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FIXTURE_DIR = pathlib.Path(__file__).resolve().parent / "consumer_static_build"

_COMPILED_MODULE_NAMES = [
    "common",
    "core",
    "serialization",
    "subtitle",
    "communication",
    "api",
]


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


# ---------------------------------------------------------------------------
# Step 1: EdgeTtsAddModule.cmake must use STATIC
# ---------------------------------------------------------------------------

def test_add_module_uses_static() -> None:
    cmake_file = REPO_ROOT / "cmake" / "EdgeTtsAddModule.cmake"
    if not cmake_file.exists():
        fail(f"EdgeTtsAddModule.cmake not found: {cmake_file}")

    content = cmake_file.read_text(encoding="utf-8")

    # The add_library call for compiled modules must include STATIC as the type.
    # Pattern: add_library(<target> STATIC <sources...>)
    if not re.search(r'add_library\s*\(\s*\$\{target\}\s+STATIC\b', content):
        fail(
            "EdgeTtsAddModule.cmake does not use STATIC in add_library().\n"
            "All edge_tts_* compiled modules must be explicitly STATIC so that "
            "BUILD_SHARED_LIBS=ON has no effect on the module type.\n"
            f"File: {cmake_file}"
        )
    ok("EdgeTtsAddModule.cmake uses explicit STATIC in add_library()")


# ---------------------------------------------------------------------------
# Step 2: Default build produces .a archives, no .so files for edge_tts modules
# ---------------------------------------------------------------------------

def _find_edge_tts_libraries(build_dir: pathlib.Path) -> tuple[list, list]:
    """Return (static_archives, shared_libs) for edge_tts_* modules."""
    static = list(build_dir.rglob("libedge_tts_*.a"))
    shared = (list(build_dir.rglob("libedge_tts_*.so")) +
              list(build_dir.rglob("libedge_tts_*.so.*")) +
              list(build_dir.rglob("libedge_tts_*.dylib")))
    return static, shared


def build_library(tmp: pathlib.Path,
                  subdir: str,
                  extra_cmake_args: list[str] | None = None) -> pathlib.Path:
    """Configure and build the production library targets. Returns build_dir."""
    build_dir = tmp / subdir
    configure_cmd = [
        cmake_bin(),
        "-S", str(REPO_ROOT),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        "-DEDGE_TTS_BUILD_APPS=OFF",
        "-DEDGE_TTS_BUILD_TESTS=OFF",
        "-DEDGE_TTS_INSTALL=OFF",
    ]
    if extra_cmake_args:
        configure_cmd.extend(extra_cmake_args)

    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            f"Configure failed ({subdir}).\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    build_targets = [f"edge_tts_{m}" for m in _COMPILED_MODULE_NAMES]
    build_cmd = [cmake_bin(), "--build", str(build_dir),
                 "--target"] + build_targets
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            f"Build failed ({subdir}).\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    return build_dir


def test_default_build_produces_static_archives(tmp: pathlib.Path) -> pathlib.Path:
    build_dir = build_library(tmp, "build_default")
    static, shared = _find_edge_tts_libraries(build_dir)

    if not static:
        fail(
            f"No libedge_tts_*.a archives found in build dir.\n"
            f"Expected static archives for: {_COMPILED_MODULE_NAMES}\n"
            f"Build dir: {build_dir}"
        )
    if shared:
        fail(
            f"Shared libraries found for edge_tts modules (static only is supported):\n"
            + "\n".join(f"  {p}" for p in shared)
        )
    ok(f"Default build: {len(static)} .a archives, no .so files")
    return build_dir


# ---------------------------------------------------------------------------
# Step 3: BUILD_SHARED_LIBS=ON must not produce .so files for edge_tts modules
# ---------------------------------------------------------------------------

def test_build_shared_libs_on_still_static(tmp: pathlib.Path) -> None:
    build_dir = build_library(tmp, "build_shared_libs_on",
                              extra_cmake_args=["-DBUILD_SHARED_LIBS=ON"])
    static, shared = _find_edge_tts_libraries(build_dir)

    if not static:
        fail(
            f"No libedge_tts_*.a archives found when BUILD_SHARED_LIBS=ON.\n"
            f"edge-tts-cpp must always build static archives regardless of BUILD_SHARED_LIBS.\n"
            f"Build dir: {build_dir}"
        )
    if shared:
        fail(
            f"BUILD_SHARED_LIBS=ON produced shared libraries for edge_tts modules:\n"
            + "\n".join(f"  {p}" for p in shared) + "\n"
            "edge-tts-cpp only supports static builds. The STATIC keyword in "
            "add_library() must override BUILD_SHARED_LIBS."
        )
    ok(f"BUILD_SHARED_LIBS=ON: still {len(static)} .a archives, no .so files")


# ---------------------------------------------------------------------------
# Step 4: Consumer add_subdirectory mode — static link
# ---------------------------------------------------------------------------

def test_consumer_add_subdirectory(tmp: pathlib.Path) -> None:
    if not FIXTURE_DIR.exists():
        fail(f"consumer_static_build fixture not found: {FIXTURE_DIR}")

    build_dir = tmp / "consumer_add_subdir"
    configure_cmd = [
        cmake_bin(),
        "-S", str(FIXTURE_DIR),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        f"-DEDGE_TTS_CPP_SOURCE_DIR={REPO_ROOT}",
        "-DEDGE_TTS_LINKAGE_MODE=add_subdirectory",
        "-DEDGE_TTS_BUILD_TESTS=OFF",
        "-DEDGE_TTS_BUILD_APPS=OFF",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            "consumer_static_build (add_subdirectory) configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_static_build (add_subdirectory) configured — module types verified at configure time")

    build_cmd = [cmake_bin(), "--build", str(build_dir),
                 "--target", "consumer_static_app"]
    result = run(build_cmd, timeout=300)
    if result.returncode != 0:
        fail(
            "consumer_static_build (add_subdirectory) build failed.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_static_app built and linked (add_subdirectory / static)")


# ---------------------------------------------------------------------------
# Step 5: Consumer find_package mode — static link against installed tree
# ---------------------------------------------------------------------------

def build_and_install(tmp: pathlib.Path) -> pathlib.Path:
    """Configure, build, and install edge-tts-cpp. Returns install_prefix."""
    build_dir = tmp / "install_build"
    install_prefix = tmp / "install_prefix"

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
            "Install-mode configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    build_targets = [f"edge_tts_{m}" for m in _COMPILED_MODULE_NAMES]
    build_cmd = [cmake_bin(), "--build", str(build_dir),
                 "--target"] + build_targets
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            "Install-mode build failed.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    install_cmd = [cmake_bin(), "--install", str(build_dir),
                   "--prefix", str(install_prefix)]
    result = run(install_cmd)
    if result.returncode != 0:
        fail(
            "cmake --install failed.\n"
            f"Command: {' '.join(install_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("edge-tts-cpp installed to temporary prefix")
    return install_prefix


def _verify_installed_static_archives(install_prefix: pathlib.Path) -> None:
    """Verify the installed lib/ contains .a archives, not .so files."""
    lib_dir = install_prefix / "lib"
    if not lib_dir.exists():
        fail(f"Install prefix has no lib/ directory: {install_prefix}")

    a_files = list(lib_dir.glob("libedge_tts_*.a"))
    so_files = (list(lib_dir.glob("libedge_tts_*.so")) +
                list(lib_dir.glob("libedge_tts_*.so.*")) +
                list(lib_dir.glob("libedge_tts_*.dylib")))

    if not a_files:
        fail(
            f"No libedge_tts_*.a archives found in installed lib/ dir.\n"
            f"lib/ contents: {list(lib_dir.iterdir())}"
        )
    if so_files:
        fail(
            f"Shared libraries found in installed lib/ (static only is supported):\n"
            + "\n".join(f"  {p}" for p in so_files)
        )
    ok(f"Installed lib/ has {len(a_files)} .a archives, no .so files")


def test_consumer_find_package(tmp: pathlib.Path) -> None:
    if not FIXTURE_DIR.exists():
        fail(f"consumer_static_build fixture not found: {FIXTURE_DIR}")

    install_prefix = build_and_install(tmp)
    _verify_installed_static_archives(install_prefix)

    build_dir = tmp / "consumer_find_package"
    configure_cmd = [
        cmake_bin(),
        "-S", str(FIXTURE_DIR),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        "-DEDGE_TTS_LINKAGE_MODE=find_package",
        f"-DCMAKE_PREFIX_PATH={install_prefix}",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            "consumer_static_build (find_package) configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_static_build (find_package) configured")

    build_cmd = [cmake_bin(), "--build", str(build_dir),
                 "--target", "consumer_static_app"]
    result = run(build_cmd, timeout=300)
    if result.returncode != 0:
        fail(
            "consumer_static_build (find_package) build failed.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_static_app built and linked (find_package / static)")


# ---------------------------------------------------------------------------
# Step 6: Documentation check
# ---------------------------------------------------------------------------

def test_consuming_md_documents_static() -> None:
    consuming_md = REPO_ROOT / "docs" / "CONSUMING.md"
    if not consuming_md.exists():
        fail(f"docs/CONSUMING.md not found: {consuming_md}")

    content = consuming_md.read_text(encoding="utf-8")

    keywords = ["static", "BUILD_SHARED_LIBS", "STATIC"]
    if not any(kw in content for kw in keywords):
        fail(
            "docs/CONSUMING.md does not document the static-only linkage mode.\n"
            "Add a 'Linkage mode' section that explains BUILD_SHARED_LIBS is ignored "
            "and all modules are built as static archives."
        )
    ok("docs/CONSUMING.md documents static-only linkage mode")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    if not cmake_bin():
        print("  SKIP linkage mode tests (cmake not on PATH)")
        sys.exit(0)

    # Step 1: source-level check (no cmake needed).
    test_add_module_uses_static()
    test_consuming_md_documents_static()

    with tempfile.TemporaryDirectory(prefix="edge_tts_linkage_") as tmp_str:
        tmp = pathlib.Path(tmp_str)

        # Step 2: default build artifacts.
        test_default_build_produces_static_archives(tmp)

        # Step 3: BUILD_SHARED_LIBS=ON must be ignored.
        test_build_shared_libs_on_still_static(tmp)

        # Step 4: consumer — add_subdirectory.
        test_consumer_add_subdirectory(tmp)

        # Step 5: consumer — find_package (install tree).
        test_consumer_find_package(tmp)

    print("\nAll linkage mode checks passed.")


if __name__ == "__main__":
    main()
