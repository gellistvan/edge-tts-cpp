#!/usr/bin/env python3
"""
Consumer example integration tests.

Verifies that both standalone consumer examples under examples/ configure and
build successfully in a fresh environment:

  examples/consumer_add_subdirectory/
    — consumes edge-tts-cpp via add_subdirectory from the repository source.

  examples/consumer_find_package/
    — consumes edge-tts-cpp via find_package after installing to a temp prefix.

Also checks that README.md contains links to both example directories.

Requires cmake ≥ 3.24 on PATH; the test is skipped otherwise.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
EXAMPLE_ADD_SUBDIR = REPO_ROOT / "examples" / "consumer_add_subdirectory"
EXAMPLE_FIND_PKG   = REPO_ROOT / "examples" / "consumer_find_package"


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
# 1. Docs check: README.md must reference both example paths
# ---------------------------------------------------------------------------

def test_readme_links_to_examples() -> None:
    readme = REPO_ROOT / "README.md"
    if not readme.exists():
        fail("README.md not found")

    content = readme.read_text(encoding="utf-8")
    for example in ("examples/consumer_add_subdirectory", "examples/consumer_find_package"):
        if example not in content:
            fail(
                f"README.md does not reference the consumer example '{example}'.\n"
                f"Add a link or mention so users can discover the examples."
            )
    ok("README.md references both consumer examples")


# ---------------------------------------------------------------------------
# 2. add_subdirectory example
# ---------------------------------------------------------------------------

def test_add_subdirectory_example(tmp: pathlib.Path) -> None:
    if not EXAMPLE_ADD_SUBDIR.exists():
        fail(f"example directory not found: {EXAMPLE_ADD_SUBDIR}")

    build_dir = tmp / "build_add_subdir"

    configure_cmd = [
        cmake_bin(),
        "-S", str(EXAMPLE_ADD_SUBDIR),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        f"-DEDGE_TTS_CPP_SOURCE_DIR={REPO_ROOT}",
        # EDGE_TTS_BUILD_APPS/TESTS are set inside the example CMakeLists.txt;
        # pass them here as well to keep cmake cache consistent in case the
        # example's FORCE overrides arrive after the first-pass default.
        "-DEDGE_TTS_BUILD_APPS=OFF",
        "-DEDGE_TTS_BUILD_TESTS=OFF",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            "consumer_add_subdirectory example: configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_add_subdirectory example: configured successfully")

    build_cmd = [cmake_bin(), "--build", str(build_dir), "--target", "tts_hello_world"]
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            "consumer_add_subdirectory example: build failed.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_add_subdirectory example: tts_hello_world built successfully")


# ---------------------------------------------------------------------------
# 3. find_package example (install edge-tts-cpp first)
# ---------------------------------------------------------------------------

def install_edge_tts(tmp: pathlib.Path) -> pathlib.Path:
    """Configure, build, and install edge-tts-cpp to a temp prefix."""
    build_dir     = tmp / "lib_build"
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
            "find_package example: configure of edge-tts-cpp failed.\n"
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
            "find_package example: build of edge-tts-cpp targets failed.\n"
            f"stdout:\n{result.stdout[:2000]}\nstderr:\n{result.stderr[:2000]}"
        )

    install_cmd = [cmake_bin(), "--install", str(build_dir), "--prefix", str(install_prefix)]
    result = run(install_cmd)
    if result.returncode != 0:
        fail(
            "find_package example: cmake --install failed.\n"
            f"stdout:\n{result.stdout[:2000]}\nstderr:\n{result.stderr[:2000]}"
        )

    ok(f"find_package example: edge-tts-cpp installed to {install_prefix}")
    return install_prefix


def test_find_package_example(tmp: pathlib.Path, install_prefix: pathlib.Path) -> None:
    if not EXAMPLE_FIND_PKG.exists():
        fail(f"example directory not found: {EXAMPLE_FIND_PKG}")

    build_dir = tmp / "build_find_pkg"

    configure_cmd = [
        cmake_bin(),
        "-S", str(EXAMPLE_FIND_PKG),
        "-B", str(build_dir),
        "-G", "Unix Makefiles",
        f"-DCMAKE_PREFIX_PATH={install_prefix}",
    ]
    result = run(configure_cmd)
    if result.returncode != 0:
        fail(
            "consumer_find_package example: configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_find_package example: configured successfully")

    build_cmd = [cmake_bin(), "--build", str(build_dir), "--target", "tts_hello_world"]
    result = run(build_cmd, timeout=300)
    if result.returncode != 0:
        fail(
            "consumer_find_package example: build failed.\n"
            f"Command: {' '.join(build_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("consumer_find_package example: tts_hello_world built successfully")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if not cmake_bin():
        print("  SKIP consumer example tests (cmake not on PATH)")
        sys.exit(0)

    # Docs check (no cmake needed).
    test_readme_links_to_examples()

    with tempfile.TemporaryDirectory(prefix="edge_tts_consumer_examples_") as tmp_str:
        tmp = pathlib.Path(tmp_str)

        # add_subdirectory example.
        test_add_subdirectory_example(tmp)

        # find_package example: install first, then consume.
        install_prefix = install_edge_tts(tmp)
        test_find_package_example(tmp, install_prefix)

    print("\nAll consumer example checks passed.")


if __name__ == "__main__":
    main()
