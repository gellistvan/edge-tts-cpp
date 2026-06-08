#!/usr/bin/env python3
"""
Consumer add_subdirectory integration test.

Configures and builds the consumer_add_subdirectory_basic fixture, which is a
standalone CMake project that consumes edge-tts-cpp via add_subdirectory().

This verifies:
  1. edge-tts-cpp's public headers are reachable from the consumer.
  2. edge_tts::common and edge_tts::core link correctly from an external project.
  3. CMAKE_SOURCE_DIR in the consumer still points to the consumer's own root
     (edge-tts-cpp's add_subdirectory must not corrupt CMAKE_SOURCE_DIR).
  4. The consumer's binary builds successfully.

Requires cmake ≥ 3.24 on PATH; the test is skipped otherwise.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FIXTURE_DIR = pathlib.Path(__file__).resolve().parent / "consumer_add_subdirectory_basic"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def main() -> None:
    if not shutil.which("cmake"):
        print("  SKIP consumer add_subdirectory test (cmake not on PATH)")
        sys.exit(0)

    if not FIXTURE_DIR.exists():
        fail(f"consumer fixture directory not found: {FIXTURE_DIR}")

    with tempfile.TemporaryDirectory(prefix="edge_tts_consumer_") as tmp_str:
        build_dir = pathlib.Path(tmp_str) / "build"

        configure_cmd = [
            "cmake",
            "-S", str(FIXTURE_DIR),
            "-B", str(build_dir),
            "-G", "Unix Makefiles",
            f"-DEDGE_TTS_CPP_SOURCE_DIR={REPO_ROOT}",
            # Build tests/apps off so we only pull in what the consumer explicitly requests.
            "-DEDGE_TTS_BUILD_TESTS=OFF",
            "-DEDGE_TTS_BUILD_APPS=OFF",
        ]

        result = subprocess.run(configure_cmd, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            fail(
                "consumer add_subdirectory configure failed.\n"
                f"Command: {' '.join(configure_cmd)}\n"
                f"stdout: {result.stdout[:1000]}\n"
                f"stderr: {result.stderr[:1000]}"
            )
        ok("consumer project configured successfully via add_subdirectory")

        build_cmd = ["cmake", "--build", str(build_dir), "--target", "consumer_app"]
        result = subprocess.run(build_cmd, capture_output=True, text=True, timeout=180)
        if result.returncode != 0:
            fail(
                "consumer add_subdirectory build failed.\n"
                f"Command: {' '.join(build_cmd)}\n"
                f"stdout: {result.stdout[:1000]}\n"
                f"stderr: {result.stderr[:1000]}"
            )
        ok("consumer_app built successfully against edge-tts-cpp via add_subdirectory")

    print("\nAll consumer add_subdirectory checks passed.")


if __name__ == "__main__":
    main()
