#!/usr/bin/env python3
"""
Install tree verification test for edge-tts-cpp.

Steps:
  1. Configure edge-tts-cpp with EDGE_TTS_BUILD_APPS=OFF (library-only, so
     ixwebsocket is optional and the test runs in sandboxed environments).
  2. Build the production library targets.
  3. cmake --install into a temporary prefix.
  4. Verify the installed tree:
       a. Required public headers are present.
       b. CMake package config files are present.
       c. No fake/test-support headers have been installed.
  5. Configure the consumer_install_basic fixture against the install prefix
     (find_package check — configure only, no link, to avoid requiring a
     system ixwebsocket on the test host).

Requires cmake >= 3.24 on PATH; the test is skipped otherwise.

Exit code 0 on success, non-zero on failure.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FIXTURE_DIR = pathlib.Path(__file__).resolve().parent / "consumer_install_basic"

# Headers that must be present in the install tree.
REQUIRED_HEADERS = [
    "include/edge_tts/edge_tts.hpp",
    "include/edge_tts/api/Communicate.hpp",
    "include/edge_tts/api/CommunicateOptions.hpp",
    "include/edge_tts/api/FileWriter.hpp",
    "include/edge_tts/core/TtsConfig.hpp",
    "include/edge_tts/core/Voice.hpp",
    "include/edge_tts/common/Error.hpp",
    "include/edge_tts/common/Result.hpp",
    "include/edge_tts/subtitles/SubMaker.hpp",
]

# CMake package files that must be present.
REQUIRED_CMAKE_FILES = [
    "lib/cmake/edge_tts_cpp/edge_tts_cpp-config.cmake",
    "lib/cmake/edge_tts_cpp/edge_tts_cpp-config-version.cmake",
    "lib/cmake/edge_tts_cpp/edge_tts_cpp-targets.cmake",
]

# These glob patterns must NOT match anything in the install tree.
FORBIDDEN_PATTERNS = [
    "**/Fake*.hpp",
    "**/FakeHttpClient*",
    "**/FakeWebSocketClient*",
    "**/FakeProcessRunner*",
    "**/test_support*",
    "**/minigtest*",
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


def main() -> None:
    if not cmake_bin():
        print("  SKIP install tree test (cmake not on PATH)")
        sys.exit(0)

    if not FIXTURE_DIR.exists():
        fail(f"consumer fixture directory not found: {FIXTURE_DIR}")

    with tempfile.TemporaryDirectory(prefix="edge_tts_install_") as tmp_str:
        tmp = pathlib.Path(tmp_str)
        build_dir = tmp / "build"
        install_prefix = tmp / "install"

        # ------------------------------------------------------------------
        # Step 1: Configure
        # ------------------------------------------------------------------
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
                "Configure failed.\n"
                f"Command: {' '.join(configure_cmd)}\n"
                f"stdout:\n{result.stdout[:2000]}\n"
                f"stderr:\n{result.stderr[:2000]}"
            )
        ok("edge-tts-cpp configured successfully")

        # ------------------------------------------------------------------
        # Step 2: Build production targets
        # edge_tts_tts is an INTERFACE library (no build rule); skip it.
        # ixwebsocket is built when the submodule is present.
        # ------------------------------------------------------------------
        build_cmd = [
            cmake_bin(), "--build", str(build_dir),
            "--target",
            "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
            "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
        ]
        result = run(build_cmd, timeout=600)
        if result.returncode != 0:
            fail(
                "Build failed.\n"
                f"Command: {' '.join(build_cmd)}\n"
                f"stdout:\n{result.stdout[:2000]}\n"
                f"stderr:\n{result.stderr[:2000]}"
            )
        ok("Production targets built successfully")

        # ------------------------------------------------------------------
        # Step 3: cmake --install
        # ------------------------------------------------------------------
        install_cmd = [
            cmake_bin(), "--install", str(build_dir),
            "--prefix", str(install_prefix),
        ]
        result = run(install_cmd)
        if result.returncode != 0:
            fail(
                "cmake --install failed.\n"
                f"Command: {' '.join(install_cmd)}\n"
                f"stdout:\n{result.stdout[:2000]}\n"
                f"stderr:\n{result.stderr[:2000]}"
            )
        ok("cmake --install completed successfully")

        # ------------------------------------------------------------------
        # Step 4a: Verify required headers are present
        # ------------------------------------------------------------------
        for rel in REQUIRED_HEADERS:
            path = install_prefix / rel
            if not path.exists():
                fail(f"Required header not found in install tree: {rel}")
        ok(f"All {len(REQUIRED_HEADERS)} required headers present in install tree")

        # ------------------------------------------------------------------
        # Step 4b: Verify CMake package config files are present
        # ------------------------------------------------------------------
        for rel in REQUIRED_CMAKE_FILES:
            path = install_prefix / rel
            if not path.exists():
                fail(f"Required CMake package file not found: {rel}")
        ok(f"All {len(REQUIRED_CMAKE_FILES)} CMake package files present")

        # Spot-check: targets file must reference the public module targets
        targets_file = install_prefix / "lib/cmake/edge_tts_cpp/edge_tts_cpp-targets.cmake"
        targets_content = targets_file.read_text(encoding="utf-8")
        for tgt in ("edge_tts_common", "edge_tts_core", "edge_tts_api", "edge_tts_tts"):
            if tgt not in targets_content:
                fail(
                    f"Exported targets file does not mention '{tgt}'.\n"
                    f"File: {targets_file}"
                )
        ok("Exported targets file references all public module targets")

        # Config file must contain the alias loop
        config_file = install_prefix / "lib/cmake/edge_tts_cpp/edge_tts_cpp-config.cmake"
        config_content = config_file.read_text(encoding="utf-8")
        if "edge_tts::tts" not in config_content and "ALIAS" not in config_content:
            fail(
                "edge_tts_cpp-config.cmake does not set up edge_tts:: aliases.\n"
                f"File: {config_file}"
            )
        ok("Package config file sets up edge_tts:: namespace aliases")

        # ------------------------------------------------------------------
        # Step 4c: Verify forbidden files are absent
        # ------------------------------------------------------------------
        for pattern in FORBIDDEN_PATTERNS:
            matches = list(install_prefix.rglob(pattern.lstrip("**/")))
            if matches:
                fail(
                    f"Forbidden file pattern '{pattern}' found in install tree:\n  "
                    + "\n  ".join(str(m.relative_to(install_prefix)) for m in matches)
                )
        ok("No fake/test-support headers found in install tree")

        # ------------------------------------------------------------------
        # Step 5: find_package consumer configure check
        # ------------------------------------------------------------------
        consumer_build = tmp / "consumer_build"
        consumer_configure_cmd = [
            cmake_bin(),
            "-S", str(FIXTURE_DIR),
            "-B", str(consumer_build),
            "-G", "Unix Makefiles",
            f"-DCMAKE_PREFIX_PATH={install_prefix}",
        ]
        result = run(consumer_configure_cmd)
        if result.returncode != 0:
            fail(
                "Consumer find_package configure failed.\n"
                f"Command: {' '.join(consumer_configure_cmd)}\n"
                f"stdout:\n{result.stdout[:2000]}\n"
                f"stderr:\n{result.stderr[:2000]}"
            )
        ok("Consumer find_package(edge_tts_cpp) configure succeeded")

    print(f"\nAll install tree checks passed.")


if __name__ == "__main__":
    main()
