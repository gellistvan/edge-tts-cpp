#!/usr/bin/env python3
"""
Installed-header self-containment tests for edge-tts-cpp.

Steps:
  1. Install edge-tts-cpp (EDGE_TTS_BUILD_APPS=OFF, EDGE_TTS_INSTALL=ON).
  2. Discover all .hpp files under <prefix>/include/edge_tts/.
  3. For each header, generate a minimal translation unit:
         #include <edge_tts/foo/Bar.hpp>
     and compile it as C++20 with -I<prefix>/include.
  4. Report any header that fails to compile in isolation.

Motivation:
  The build-tree self-containment test (edge_tts_header_selfcontainment_tests)
  exercises headers via CMake with all include paths in scope.  This test
  exercises the *installed* tree — the exact set of files a consumer receives
  after `cmake --install` — using only the paths that a consumer would have.

  A header is considered "self-contained" if it compiles when included as the
  *first and only* project header in a translation unit, using only the installed
  include prefix and the C++ standard library.

Constraints:
  - Compilation uses -std=c++20.
  - No precompiled headers; each file compiled independently.
  - No project-internal include paths; only <prefix>/include is used.
  - The test discovers headers dynamically from the install tree so it
    automatically catches new headers as they are added.

Requires cmake ≥ 3.24 and a C++20-capable CXX compiler on PATH.
Skipped if cmake or a suitable compiler cannot be found.

Exit code 0 on success, non-zero on failure.
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


def warn(msg: str) -> None:
    print(f"  WARN  {msg}")


def run(cmd: list[str], cwd: pathlib.Path | None = None,
        timeout: int = 300) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True,
                          timeout=timeout, cwd=cwd)


def cmake_bin() -> str | None:
    return shutil.which("cmake")


def find_cxx_compiler() -> str | None:
    """Find a C++20-capable compiler on PATH."""
    for name in ("c++", "g++", "clang++"):
        path = shutil.which(name)
        if path:
            return path
    return None


def install_edge_tts(tmp: pathlib.Path) -> pathlib.Path:
    """Configure, build (library targets only), and install. Returns install prefix."""
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
            "Configure failed.\n"
            f"Command: {' '.join(configure_cmd)}\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("Configured edge-tts-cpp for install")

    build_cmd = [
        cmake_bin(), "--build", str(build_dir), "--target",
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
    ]
    result = run(build_cmd, timeout=600)
    if result.returncode != 0:
        fail(
            "Build failed.\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok("Built library targets")

    install_cmd = [cmake_bin(), "--install", str(build_dir),
                   "--prefix", str(install_prefix)]
    result = run(install_cmd)
    if result.returncode != 0:
        fail(
            "Install failed.\n"
            f"stdout:\n{result.stdout[:2000]}\n"
            f"stderr:\n{result.stderr[:2000]}"
        )
    ok(f"Installed to {install_prefix}")
    return install_prefix


def compile_header(
    cxx: str,
    include_prefix: pathlib.Path,
    header_rel: str,
    src_file: pathlib.Path,
) -> tuple[bool, str]:
    """
    Compile a generated .cpp that includes exactly one header.
    Returns (success, error_output).
    """
    cmd = [
        cxx,
        "-std=c++20",
        f"-I{include_prefix}",
        "-fsyntax-only",   # parse + type-check; skip code gen and linking
        str(src_file),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        return False, (result.stdout + result.stderr).strip()
    return True, ""


def test_installed_header_selfcontainment(
    install_prefix: pathlib.Path,
    cxx: str,
    tmp: pathlib.Path,
) -> None:
    """
    For every .hpp discovered under <prefix>/include/edge_tts/, generate a
    .cpp with only that include and compile it.
    """
    include_dir = install_prefix / "include"
    edge_tts_include = include_dir / "edge_tts"

    if not edge_tts_include.exists():
        fail(f"Expected include/edge_tts/ not found under {install_prefix}")

    headers = sorted(edge_tts_include.rglob("*.hpp"))
    if not headers:
        fail(f"No .hpp files found under {edge_tts_include}")
    ok(f"Discovered {len(headers)} installed headers")

    gen_dir = tmp / "gen"
    gen_dir.mkdir()

    failures: list[tuple[str, str]] = []
    for h in headers:
        # e.g. edge_tts/api/SpeechSynthesizer.hpp
        rel = str(h.relative_to(include_dir))

        # safe filename: edge_tts_api_Communicate_hpp.cpp
        safe = rel.replace("/", "_").replace(".", "_")
        src_file = gen_dir / f"{safe}.cpp"
        # Include the header as the *only* project include.
        src_file.write_text(f"#include <{rel}>\n", encoding="utf-8")

        success, err = compile_header(cxx, include_dir, rel, src_file)
        if success:
            ok(f"  self-contained: {rel}")
        else:
            failures.append((rel, err))
            print(f"  FAIL: {rel}", file=sys.stderr)
            if err:
                # Print first 10 lines of the compiler error.
                for line in err.splitlines()[:10]:
                    print(f"    {line}", file=sys.stderr)

    if failures:
        fail(
            f"\n{len(failures)} installed header(s) are not self-contained:\n  "
            + "\n  ".join(h for h, _ in failures)
        )
    ok(f"All {len(headers)} installed headers are self-contained")


def main() -> None:
    if not cmake_bin():
        print("  SKIP installed-header self-containment test (cmake not on PATH)")
        sys.exit(0)

    cxx = find_cxx_compiler()
    if not cxx:
        print("  SKIP installed-header self-containment test (no C++ compiler on PATH)")
        sys.exit(0)

    # Windows: -fsyntax-only may not be available on MSVC.  Skip gracefully.
    if platform.system() == "Windows":
        print("  SKIP installed-header self-containment test (Windows not yet supported)")
        sys.exit(0)

    with tempfile.TemporaryDirectory(prefix="edge_tts_hdr_sc_") as tmp_str:
        tmp = pathlib.Path(tmp_str)
        install_prefix = install_edge_tts(tmp)
        test_installed_header_selfcontainment(install_prefix, cxx, tmp)

    print("\nAll installed-header self-containment checks passed.")


if __name__ == "__main__":
    main()
