#!/usr/bin/env python3
"""
Install component tests for edge-tts-cpp.

Verifies that the install component model behaves correctly:

  Development component
    - cmake --install --component Development installs headers + archives + CMake files.
    - cmake --install --component Development does NOT install app binaries.
    - cmake --install --component Development with EDGE_TTS_INSTALL_LIBRARY=OFF installs nothing.

  Apps component
    - cmake --install --component Apps with EDGE_TTS_INSTALL_APPS=ON installs edge-tts.
    - cmake --install --component Apps with EDGE_TTS_INSTALL_APPS=OFF installs nothing.
    - edge-playback is installed only when EDGE_TTS_BUILD_PLAYBACK_APP=ON.

  Default full install
    - cmake --install with EDGE_TTS_INSTALL_APPS=OFF does not install app binaries
      (regression check for the existing test_install_tree.py test; duplicated here
      for the component model).

Exit code 0 on success, non-zero on failure.
Requires cmake >= 3.24 on PATH; the test is skipped otherwise.
"""

import pathlib
import platform
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


def configure_and_build(
    tmp: pathlib.Path,
    label: str,
    extra_defs: list[str] | None = None,
    build_targets: list[str] | None = None,
    timeout: int = 600,
) -> tuple[pathlib.Path, pathlib.Path]:
    """Configure and (partially) build edge-tts-cpp. Returns (build_dir, unused_prefix)."""
    build_dir = tmp / "build"
    install_prefix = tmp / "install"

    cmake_args = [
        cmake_bin(),
        "-S", str(REPO_ROOT),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        "-DEDGE_TTS_BUILD_TESTS=OFF",
        f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
    ]
    if extra_defs:
        cmake_args.extend(extra_defs)

    result = run(cmake_args)
    if result.returncode != 0:
        fail(
            f"[{label}] Configure failed.\n"
            f"Command: {' '.join(cmake_args)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    targets = build_targets or [
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
    ]
    build_cmd = [cmake_bin(), "--build", str(build_dir), "--target"] + targets
    result = run(build_cmd, timeout=timeout)
    if result.returncode != 0:
        fail(
            f"[{label}] Build failed.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )

    return build_dir, install_prefix


def do_install(
    label: str,
    build_dir: pathlib.Path,
    install_prefix: pathlib.Path,
    component: str | None = None,
    extra_flags: list[str] | None = None,
) -> None:
    cmd = [cmake_bin(), "--install", str(build_dir), "--prefix", str(install_prefix)]
    if component:
        cmd += ["--component", component]
    if extra_flags:
        cmd.extend(extra_flags)
    result = run(cmd)
    if result.returncode != 0:
        fail(
            f"[{label}] cmake --install failed.\n"
            f"Command: {' '.join(cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )


# ── Helpers ────────────────────────────────────────────────────────────────

def assert_headers_present(prefix: pathlib.Path, label: str) -> None:
    sentinel = prefix / "include" / "edge_tts" / "edge_tts.hpp"
    if not sentinel.exists():
        fail(f"[{label}] Expected header not found: {sentinel.relative_to(prefix)}")
    ok(f"[{label}] Public headers installed under include/edge_tts/")


def assert_cmake_files_present(prefix: pathlib.Path, label: str) -> None:
    cmake_dir = prefix / "lib" / "cmake" / "edge_tts_cpp"
    for name in ("edge_tts_cpp-config.cmake",
                 "edge_tts_cpp-config-version.cmake",
                 "edge_tts_cpp-targets.cmake"):
        if not (cmake_dir / name).exists():
            fail(f"[{label}] CMake package file missing: lib/cmake/edge_tts_cpp/{name}")
    ok(f"[{label}] CMake package config files installed")


def assert_lib_present(prefix: pathlib.Path, label: str) -> None:
    lib_dir = prefix / "lib"
    archives = list(lib_dir.glob("libedge_tts_*.a")) if lib_dir.exists() else []
    if not archives:
        fail(f"[{label}] No libedge_tts_*.a archives found under lib/")
    ok(f"[{label}] Library archives installed ({len(archives)} found)")


def assert_no_headers(prefix: pathlib.Path, label: str) -> None:
    include_dir = prefix / "include"
    if include_dir.exists() and any(include_dir.rglob("*.hpp")):
        fail(f"[{label}] Headers installed when they should NOT be.")
    ok(f"[{label}] No headers installed (correct)")


def assert_no_cmake_files(prefix: pathlib.Path, label: str) -> None:
    cmake_dir = prefix / "lib" / "cmake"
    if cmake_dir.exists() and any(cmake_dir.rglob("*.cmake")):
        fail(f"[{label}] CMake package files installed when they should NOT be.")
    ok(f"[{label}] No CMake package files installed (correct)")


def assert_no_libs(prefix: pathlib.Path, label: str) -> None:
    lib_dir = prefix / "lib"
    archives = list(lib_dir.glob("libedge_tts_*.a")) if lib_dir.exists() else []
    if archives:
        fail(f"[{label}] Library archives found when they should NOT be: "
             + ", ".join(a.name for a in archives))
    ok(f"[{label}] No library archives installed (correct)")


def assert_app_present(prefix: pathlib.Path, app_name: str, label: str) -> None:
    app_path = prefix / "bin" / app_name
    if not app_path.exists():
        fail(f"[{label}] Expected app binary not found: bin/{app_name}")
    ok(f"[{label}] App binary installed: bin/{app_name}")


def assert_no_app(prefix: pathlib.Path, app_name: str, label: str) -> None:
    app_path = prefix / "bin" / app_name
    if app_path.exists():
        fail(f"[{label}] App binary should NOT be installed: bin/{app_name}")
    ok(f"[{label}] App binary not installed (correct): bin/{app_name}")


# ── Test cases ─────────────────────────────────────────────────────────────

def test_development_component_installs_library(tmp_root: pathlib.Path) -> None:
    """--component Development installs headers, archives, and CMake files."""
    label = "development_component"
    tmp = tmp_root / label
    tmp.mkdir()

    build_dir, prefix = configure_and_build(
        tmp, label,
        extra_defs=["-DEDGE_TTS_BUILD_APPS=OFF", "-DEDGE_TTS_INSTALL=ON"],
    )
    do_install(label, build_dir, prefix, component="Development")

    assert_headers_present(prefix, label)
    assert_cmake_files_present(prefix, label)
    assert_lib_present(prefix, label)
    assert_no_app(prefix, "edge-tts", label)
    assert_no_app(prefix, "edge-playback", label)
    ok(f"[{label}] PASSED")


def test_development_component_does_not_install_apps(tmp_root: pathlib.Path) -> None:
    """
    Even when EDGE_TTS_INSTALL_APPS=ON + apps are built, --component Development
    must not install app binaries.
    """
    label = "development_no_apps"
    tmp = tmp_root / label
    tmp.mkdir()

    build_targets = [
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
        "edge-tts",
    ]
    build_dir, prefix = configure_and_build(
        tmp, label,
        extra_defs=[
            "-DEDGE_TTS_BUILD_APPS=ON",
            "-DEDGE_TTS_INSTALL=ON",
            "-DEDGE_TTS_INSTALL_APPS=ON",
        ],
        build_targets=build_targets,
        timeout=900,
    )
    # Install ONLY the Development component.
    do_install(label, build_dir, prefix, component="Development")

    assert_headers_present(prefix, label)
    assert_cmake_files_present(prefix, label)
    assert_no_app(prefix, "edge-tts", label)
    ok(f"[{label}] PASSED")


def test_install_library_off_skips_development(tmp_root: pathlib.Path) -> None:
    """EDGE_TTS_INSTALL_LIBRARY=OFF: --component Development installs nothing."""
    label = "install_library_off"
    tmp = tmp_root / label
    tmp.mkdir()

    build_dir, prefix = configure_and_build(
        tmp, label,
        extra_defs=[
            "-DEDGE_TTS_BUILD_APPS=OFF",
            "-DEDGE_TTS_INSTALL=ON",
            "-DEDGE_TTS_INSTALL_LIBRARY=OFF",
        ],
    )
    do_install(label, build_dir, prefix, component="Development")

    assert_no_headers(prefix, label)
    assert_no_cmake_files(prefix, label)
    assert_no_libs(prefix, label)
    ok(f"[{label}] PASSED")


def test_apps_component_installs_edge_tts(tmp_root: pathlib.Path) -> None:
    """EDGE_TTS_INSTALL_APPS=ON + --component Apps installs edge-tts binary."""
    label = "apps_component_edge_tts"
    tmp = tmp_root / label
    tmp.mkdir()

    build_targets = [
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
        "edge-tts",
    ]
    build_dir, prefix = configure_and_build(
        tmp, label,
        extra_defs=[
            "-DEDGE_TTS_BUILD_APPS=ON",
            # Disable playback so this test focuses solely on edge-tts.
            # The playback install scenario is covered by
            # test_playback_component_posix_only.
            "-DEDGE_TTS_BUILD_PLAYBACK_APP=OFF",
            "-DEDGE_TTS_INSTALL=ON",
            "-DEDGE_TTS_INSTALL_APPS=ON",
        ],
        build_targets=build_targets,
        timeout=900,
    )
    do_install(label, build_dir, prefix, component="Apps")

    assert_app_present(prefix, "edge-tts", label)
    # Library artifacts must NOT be in the Apps component install.
    assert_no_headers(prefix, label)
    assert_no_cmake_files(prefix, label)
    ok(f"[{label}] PASSED")


def test_apps_component_off_installs_nothing(tmp_root: pathlib.Path) -> None:
    """EDGE_TTS_INSTALL_APPS=OFF: --component Apps installs nothing."""
    label = "apps_component_off"
    tmp = tmp_root / label
    tmp.mkdir()

    build_dir, prefix = configure_and_build(
        tmp, label,
        extra_defs=[
            "-DEDGE_TTS_BUILD_APPS=OFF",
            "-DEDGE_TTS_INSTALL=ON",
            "-DEDGE_TTS_INSTALL_APPS=OFF",
        ],
    )
    do_install(label, build_dir, prefix, component="Apps")

    bin_dir = prefix / "bin"
    installed_bins = [
        p.name for p in bin_dir.iterdir()
        if p.name in ("edge-tts", "edge-playback")
    ] if bin_dir.exists() else []
    if installed_bins:
        fail(f"[{label}] App binaries installed despite EDGE_TTS_INSTALL_APPS=OFF: "
             + ", ".join(installed_bins))
    ok(f"[{label}] No app binaries installed with EDGE_TTS_INSTALL_APPS=OFF (correct)")
    ok(f"[{label}] PASSED")


def test_playback_component_posix_only(tmp_root: pathlib.Path) -> None:
    """
    POSIX only: when EDGE_TTS_BUILD_PLAYBACK_APP=ON + EDGE_TTS_INSTALL_APPS=ON,
    edge-playback is installed.
    Skipped on Windows where EDGE_TTS_BUILD_PLAYBACK_APP is forced OFF.
    """
    label = "playback_posix"

    if platform.system() == "Windows":
        ok(f"[{label}] Skipped on Windows (playback requires POSIX)")
        return

    tmp = tmp_root / label
    tmp.mkdir()

    build_targets = [
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
        "edge-tts", "edge-playback",
    ]
    build_dir, prefix = configure_and_build(
        tmp, label,
        extra_defs=[
            "-DEDGE_TTS_BUILD_APPS=ON",
            "-DEDGE_TTS_BUILD_PLAYBACK_APP=ON",
            "-DEDGE_TTS_INSTALL=ON",
            "-DEDGE_TTS_INSTALL_APPS=ON",
        ],
        build_targets=build_targets,
        timeout=900,
    )
    do_install(label, build_dir, prefix, component="Apps")

    assert_app_present(prefix, "edge-tts", label)
    assert_app_present(prefix, "edge-playback", label)
    ok(f"[{label}] PASSED")


def test_default_install_no_apps(tmp_root: pathlib.Path) -> None:
    """
    Regression: plain cmake --install (no component filter) with default options
    must not install app binaries.
    """
    label = "default_install_no_apps"
    tmp = tmp_root / label
    tmp.mkdir()

    build_dir, prefix = configure_and_build(
        tmp, label,
        # All defaults: EDGE_TTS_INSTALL_APPS=OFF, EDGE_TTS_INSTALL_LIBRARY=ON.
        extra_defs=["-DEDGE_TTS_BUILD_APPS=OFF", "-DEDGE_TTS_INSTALL=ON"],
    )
    do_install(label, build_dir, prefix)

    assert_headers_present(prefix, label)
    assert_cmake_files_present(prefix, label)
    assert_no_app(prefix, "edge-tts", label)
    assert_no_app(prefix, "edge-playback", label)
    ok(f"[{label}] PASSED")


# ── Entry point ────────────────────────────────────────────────────────────

def main() -> None:
    if not cmake_bin():
        print("  SKIP install component tests (cmake not on PATH)")
        sys.exit(0)

    with tempfile.TemporaryDirectory(prefix="edge_tts_components_") as tmp_str:
        tmp = pathlib.Path(tmp_str)

        test_development_component_installs_library(tmp)
        test_install_library_off_skips_development(tmp)
        test_apps_component_off_installs_nothing(tmp)
        test_default_install_no_apps(tmp)
        test_development_component_does_not_install_apps(tmp)
        test_apps_component_installs_edge_tts(tmp)
        test_playback_component_posix_only(tmp)

    print("\nAll install component checks passed.")


if __name__ == "__main__":
    main()
