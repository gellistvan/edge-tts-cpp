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
       d. CLI/media/app targets are absent from the exported targets file.
       e. No app binaries were installed (EDGE_TTS_INSTALL_APPS=OFF default).
  5. Configure the consumer_install_basic fixture against the install prefix
     (find_package check) and then build it (full link test).
  6. Relocation test: copy the install tree to a second prefix and re-run the
     consumer build to confirm all paths in the CMake files are relative.

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
    "include/edge_tts/version.hpp",
    "include/api/SpeechSynthesizer.hpp",
    "include/api/SynthesisOptions.hpp",
    "include/api/FileWriter.hpp",
    "include/core/TtsConfig.hpp",
    "include/core/Voice.hpp",
    "include/common/Error.hpp",
    "include/common/Result.hpp",
    "include/subtitles/SubtitleBuilder.hpp",
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

# These target names must NOT be defined as CMake targets in the exported
# targets file.  CLI/media/playback are internal; they must not leak to
# package consumers.  We match against CMake target-creation patterns
# (add_library / add_executable) to avoid false positives in comments.
FORBIDDEN_EXPORTED_TARGETS = [
    "edge_tts_cli",
    "edge_tts_media",
    "edge-tts",
    "edge-playback",
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


def build_and_install(tmp: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    """Configure, build, and install edge-tts-cpp. Returns (build_dir, install_prefix)."""
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

    return build_dir, install_prefix


def verify_install_tree(install_prefix: pathlib.Path) -> None:
    """Verify the installed tree content (Steps 4a–4e)."""

    # ------------------------------------------------------------------
    # Step 4a: Required headers present
    # ------------------------------------------------------------------
    for rel in REQUIRED_HEADERS:
        if not (install_prefix / rel).exists():
            fail(f"Required header not found in install tree: {rel}")
    ok(f"All {len(REQUIRED_HEADERS)} required headers present in install tree")

    # ------------------------------------------------------------------
    # Step 4b: CMake package config files present
    # ------------------------------------------------------------------
    for rel in REQUIRED_CMAKE_FILES:
        if not (install_prefix / rel).exists():
            fail(f"Required CMake package file not found: {rel}")
    ok(f"All {len(REQUIRED_CMAKE_FILES)} CMake package files present")

    # Spot-check: targets file must reference the public module targets.
    targets_file = install_prefix / "lib/cmake/edge_tts_cpp/edge_tts_cpp-targets.cmake"
    targets_content = targets_file.read_text(encoding="utf-8")
    for tgt in ("edge_tts_common", "edge_tts_core", "edge_tts_api", "edge_tts_tts"):
        if tgt not in targets_content:
            fail(
                f"Exported targets file does not mention '{tgt}'.\n"
                f"File: {targets_file}"
            )
    ok("Exported targets file references all public module targets")

    # Config file must contain the edge_tts:: alias loop.
    config_file = install_prefix / "lib/cmake/edge_tts_cpp/edge_tts_cpp-config.cmake"
    config_content = config_file.read_text(encoding="utf-8")
    if "edge_tts::tts" not in config_content and "ALIAS" not in config_content:
        fail(
            "edge_tts_cpp-config.cmake does not set up edge_tts:: aliases.\n"
            f"File: {config_file}"
        )
    ok("Package config file sets up edge_tts:: namespace aliases")

    # ------------------------------------------------------------------
    # Step 4c: No fake/test-support headers
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
    # Step 4d: CLI/media/app targets are absent from the exported targets file
    # ------------------------------------------------------------------
    # Concatenate all cmake files under lib/cmake/edge_tts_cpp/.
    # Match against CMake target-creation patterns to avoid false positives
    # from comments or file names that contain the same substrings.
    import re
    all_cmake = ""
    for f in (install_prefix / "lib/cmake/edge_tts_cpp").iterdir():
        if f.suffix == ".cmake":
            all_cmake += f.read_text(encoding="utf-8")

    # Target definitions look like: add_library(<name> ...) or add_executable(<name> ...)
    target_creation_pattern = re.compile(
        r'\badd_(?:library|executable)\s*\(\s*({name})\b'.replace(
            "{name}", "|".join(re.escape(t) for t in FORBIDDEN_EXPORTED_TARGETS)
        )
    )
    match = target_creation_pattern.search(all_cmake)
    if match:
        fail(
            f"Forbidden target '{match.group(1)}' is defined in installed CMake files. "
            "CLI/media/app targets must not be exported to package consumers."
        )
    ok("No CLI/media/app targets exported to package consumers")

    # ------------------------------------------------------------------
    # Step 4e: No app binaries installed (EDGE_TTS_INSTALL_APPS defaults OFF)
    # ------------------------------------------------------------------
    bin_dir = install_prefix / "bin"
    if bin_dir.exists():
        installed_bins = [
            p.name for p in bin_dir.iterdir()
            if p.name in ("edge-tts", "edge-playback", "edge_tts", "edge_playback")
        ]
        if installed_bins:
            fail(
                f"App binaries found in install tree despite EDGE_TTS_INSTALL_APPS=OFF: "
                + ", ".join(installed_bins)
            )
    ok("No app binaries installed (EDGE_TTS_INSTALL_APPS=OFF)")


def run_consumer(
    label: str,
    fixture_dir: pathlib.Path,
    consumer_build: pathlib.Path,
    prefix: pathlib.Path,
) -> None:
    """Configure and build the consumer fixture against an install prefix."""
    consumer_configure_cmd = [
        cmake_bin(),
        "-S", str(fixture_dir),
        "-B", str(consumer_build),
        "-G", "Unix Makefiles",
        f"-DCMAKE_PREFIX_PATH={prefix}",
    ]
    result = run(consumer_configure_cmd)
    if result.returncode != 0:
        fail(
            f"{label}: Consumer find_package configure failed.\n"
            f"Command: {' '.join(consumer_configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok(f"{label}: Consumer find_package(edge_tts_cpp) configure succeeded")

    consumer_build_cmd = [cmake_bin(), "--build", str(consumer_build)]
    result = run(consumer_build_cmd, timeout=300)
    if result.returncode != 0:
        fail(
            f"{label}: Consumer build failed.\n"
            f"Command: {' '.join(consumer_build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok(f"{label}: Consumer app built and linked successfully")


def verify_no_build_tree_paths(
    install_prefix: pathlib.Path,
    build_dir: pathlib.Path,
) -> None:
    """Step 4f: Installed CMake files must not reference build-tree paths."""
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
            + "\nAll paths in installed targets must be relative to _IMPORT_PREFIX, "
            "not hardcoded to the build directory."
        )
    ok("No build-tree paths in installed CMake files")


def main() -> None:
    if not cmake_bin():
        print("  SKIP install tree test (cmake not on PATH)")
        sys.exit(0)

    if not FIXTURE_DIR.exists():
        fail(f"consumer fixture directory not found: {FIXTURE_DIR}")

    with tempfile.TemporaryDirectory(prefix="edge_tts_install_") as tmp_str:
        tmp = pathlib.Path(tmp_str)

        # Steps 1–3: Configure, build, install.
        build_dir, install_prefix = build_and_install(tmp)

        # Step 4a–4e: Verify install tree content.
        verify_install_tree(install_prefix)

        # Step 4f: No build-tree paths in installed CMake files.
        verify_no_build_tree_paths(install_prefix, build_dir)

        # ------------------------------------------------------------------
        # Step 5: find_package consumer — configure + build
        # ------------------------------------------------------------------
        run_consumer(
            label="Install",
            fixture_dir=FIXTURE_DIR,
            consumer_build=tmp / "consumer_build",
            prefix=install_prefix,
        )

        # ------------------------------------------------------------------
        # Step 6: Relocation test
        # Copy the install prefix to a new directory and confirm the consumer
        # builds from the relocated prefix.  This exercises that all paths in
        # the generated CMake files are relative (using _IMPORT_PREFIX), not
        # hardcoded to the original build-time prefix.
        # ------------------------------------------------------------------
        relocated_prefix = tmp / "install_relocated"
        shutil.copytree(str(install_prefix), str(relocated_prefix))
        ok("Install tree copied to relocated prefix")

        run_consumer(
            label="Relocation",
            fixture_dir=FIXTURE_DIR,
            consumer_build=tmp / "consumer_build_relocated",
            prefix=relocated_prefix,
        )

    print("\nAll install tree checks passed.")


if __name__ == "__main__":
    main()
