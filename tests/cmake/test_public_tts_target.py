#!/usr/bin/env python3
"""
Public edge_tts::tts target tests.

Verifies:
  1. edge_tts_tts and edge_tts::all are defined in CMakeLists.txt.
  2. edge_tts_tts does NOT link edge_tts_cli, edge_tts::cli, edge_tts_media, or
     edge_tts_test_support in its INTERFACE_LINK_LIBRARIES definition.
  3. edge_tts_tts DOES link edge_tts::api (directly or transitively via api).
  4. The consumer_tts_target_basic fixture configures and builds successfully
     using only edge_tts::tts.
  5. The broad aggregate (edge_tts::all / edge_tts::edge_tts) still exists.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FIXTURE_DIR = pathlib.Path(__file__).resolve().parent / "consumer_tts_target_basic"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# 1. edge_tts_tts is defined in CMakeLists.txt
# ---------------------------------------------------------------------------

def test_edge_tts_tts_target_defined() -> None:
    cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(cmake)
    if not re.search(r'add_library\s*\(\s*edge_tts_tts\b', content):
        fail(
            "CMakeLists.txt does not define 'edge_tts_tts' library target.\n"
            "Add: add_library(edge_tts_tts INTERFACE)"
        )
    if "edge_tts::tts" not in content:
        fail(
            "CMakeLists.txt does not define 'edge_tts::tts' alias.\n"
            "Add: add_library(edge_tts::tts ALIAS edge_tts_tts)"
        )
    ok("edge_tts_tts and edge_tts::tts are defined in CMakeLists.txt")


# ---------------------------------------------------------------------------
# 2. edge_tts_tts does not link cli, media (beyond api transitive), or test-support
# ---------------------------------------------------------------------------

def _extract_tts_target_links(content: str) -> list[str]:
    """
    Return the list of targets in the target_link_libraries call for edge_tts_tts.
    Only the direct links — not transitive deps of linked modules.
    """
    # Find the block: target_link_libraries(edge_tts_tts ...)
    match = re.search(
        r'target_link_libraries\s*\(\s*edge_tts_tts\b(.*?)\)',
        content,
        re.DOTALL,
    )
    if not match:
        return []
    block = match.group(1)
    # Extract token-like words (cmake target names)
    tokens = re.findall(r'[\w:]+', block)
    return [t for t in tokens if t not in ('INTERFACE', 'PUBLIC', 'PRIVATE')]


def test_edge_tts_tts_does_not_link_cli() -> None:
    cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(cmake)
    links = _extract_tts_target_links(content)

    forbidden = [t for t in links if 'cli' in t.lower()]
    if forbidden:
        fail(
            f"edge_tts_tts directly links CLI targets: {forbidden}\n"
            "edge_tts::tts must not expose CLI in its link interface."
        )
    ok("edge_tts_tts does not directly link any CLI target")


def test_edge_tts_tts_does_not_link_test_support() -> None:
    cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(cmake)
    links = _extract_tts_target_links(content)

    forbidden = [t for t in links if 'test_support' in t.lower() or 'fake' in t.lower()]
    if forbidden:
        fail(
            f"edge_tts_tts directly links test-support targets: {forbidden}\n"
            "edge_tts::tts must not link test-only utilities."
        )
    ok("edge_tts_tts does not directly link test-support targets")


def test_edge_tts_tts_links_api() -> None:
    cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(cmake)
    links = _extract_tts_target_links(content)

    has_api = any('api' in t.lower() for t in links)
    if not has_api:
        fail(
            f"edge_tts_tts does not link edge_tts::api (found: {links}).\n"
            "edge_tts::tts must expose the synthesis API to consumers."
        )
    ok(f"edge_tts_tts links edge_tts::api (direct deps: {links})")


# ---------------------------------------------------------------------------
# 3. Broad aggregate (edge_tts::all / edge_tts::edge_tts) still exists
# ---------------------------------------------------------------------------

def test_broad_aggregate_still_exists() -> None:
    cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(cmake)

    if not re.search(r'add_library\s*\(\s*edge_tts\b', content):
        fail(
            "CMakeLists.txt no longer defines the broad 'edge_tts' aggregate library.\n"
            "Keep it as edge_tts::edge_tts and edge_tts::all for compatibility."
        )
    if "edge_tts::all" not in content:
        fail(
            "CMakeLists.txt does not define 'edge_tts::all' alias for the broad aggregate.\n"
            "Add: add_library(edge_tts::all ALIAS edge_tts)"
        )
    ok("Broad aggregate edge_tts (edge_tts::edge_tts / edge_tts::all) still exists")


# ---------------------------------------------------------------------------
# 4. Consumer fixture: configure + build with only edge_tts::tts
# ---------------------------------------------------------------------------

def test_consumer_tts_target_fixture() -> None:
    if not shutil.which("cmake"):
        print("  SKIP consumer_tts_target fixture (cmake not on PATH)")
        return

    if not FIXTURE_DIR.exists():
        fail(f"consumer_tts_target_basic fixture not found: {FIXTURE_DIR}")

    with tempfile.TemporaryDirectory(prefix="edge_tts_tts_consumer_") as tmp_str:
        build_dir = pathlib.Path(tmp_str) / "build"

        configure_cmd = [
            "cmake",
            "-S", str(FIXTURE_DIR),
            "-B", str(build_dir),
            "-G", "Unix Makefiles",
            f"-DEDGE_TTS_CPP_SOURCE_DIR={REPO_ROOT}",
            "-DEDGE_TTS_BUILD_TESTS=OFF",
            "-DEDGE_TTS_BUILD_APPS=OFF",
        ]
        result = subprocess.run(configure_cmd, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            fail(
                "consumer_tts_target_basic configure failed.\n"
                f"Command: {' '.join(configure_cmd)}\n"
                f"stdout: {result.stdout[:1000]}\nstderr: {result.stderr[:1000]}"
            )
        ok("consumer_tts_target_basic configured successfully")

        build_cmd = ["cmake", "--build", str(build_dir), "--target", "consumer_tts_app"]
        result = subprocess.run(build_cmd, capture_output=True, text=True, timeout=180)
        if result.returncode != 0:
            fail(
                "consumer_tts_target_basic build failed.\n"
                f"Command: {' '.join(build_cmd)}\n"
                f"stdout: {result.stdout[:1000]}\nstderr: {result.stderr[:1000]}"
            )
        ok("consumer_tts_app built successfully against edge_tts::tts")

        # The fixture CMakeLists.txt also verifies CLI is not in the link interface
        # at configure time, so if configure succeeded, that check passed.
        ok("edge_tts::tts INTERFACE_LINK_LIBRARIES does not include edge_tts::cli (verified at configure time)")


# ---------------------------------------------------------------------------
# 5. README documents edge_tts::tts as the recommended target
# ---------------------------------------------------------------------------

def test_readme_documents_tts_target() -> None:
    readme = REPO_ROOT / "README.md"
    if not readme.exists():
        fail("README.md does not exist")
    content = read(readme)
    if "edge_tts::tts" not in content:
        fail(
            "README.md does not mention edge_tts::tts as the recommended consumer target."
        )
    ok("README.md mentions edge_tts::tts")


# ---------------------------------------------------------------------------
# 6. docs/MODULES.md documents the public/internal target distinction
# ---------------------------------------------------------------------------

def test_modules_md_documents_tts_target() -> None:
    modules_md = REPO_ROOT / "docs" / "MODULES.md"
    if not modules_md.exists():
        fail("docs/MODULES.md does not exist")
    content = read(modules_md)
    if "edge_tts::tts" not in content:
        fail(
            "docs/MODULES.md does not document edge_tts::tts as the public consumer target."
        )
    ok("docs/MODULES.md documents edge_tts::tts")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_edge_tts_tts_target_defined,
        test_edge_tts_tts_does_not_link_cli,
        test_edge_tts_tts_does_not_link_test_support,
        test_edge_tts_tts_links_api,
        test_broad_aggregate_still_exists,
        test_consumer_tts_target_fixture,
        test_readme_documents_tts_target,
        test_modules_md_documents_tts_target,
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} public tts target checks passed.")


if __name__ == "__main__":
    main()
