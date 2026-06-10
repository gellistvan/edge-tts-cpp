#!/usr/bin/env python3
"""
Dependency configuration tests for edge-tts-cpp.

Verifies:
  1. cmake/EdgeTtsDependencies.cmake exists and references ixwebsocket.
  2. cmake/Dependencies.cmake includes EdgeTtsDependencies.
  3. INTERFACE_SYSTEM_INCLUDE_DIRECTORIES suppression is present.
  4. .gitmodules registers the ixwebsocket submodule.
  5. CMakeLists.txt links ixwebsocket to the communication module.
  6. Public headers in include/edge_tts/ do not include ixwebsocket headers.
  7. DEPENDENCIES.md documents ixwebsocket.
  8. nlohmann/json is not optional (unconditional links in CMakeLists.txt).
  9. EDGE_TTS_FETCH_DEPS option is defined.
 10. EDGE_TTS_REQUIRE_NETWORKING option is defined.
 11. cmake/Dependencies.cmake has the correct json lookup order.
 12. cmake/EdgeTtsDependencies.cmake has the correct ixwebsocket lookup order.
 13. cmake/Dependencies.cmake has a FATAL_ERROR for json not found.
 14. FATAL_ERROR is gated on EDGE_TTS_REQUIRE_NETWORKING for missing ixwebsocket.

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

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# 1. cmake/EdgeTtsDependencies.cmake exists and has required content
# ---------------------------------------------------------------------------

def test_edge_tts_dependencies_cmake_exists() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    if not path.exists():
        fail("cmake/EdgeTtsDependencies.cmake does not exist")
    ok("cmake/EdgeTtsDependencies.cmake exists")


def test_edge_tts_dependencies_cmake_references_ixwebsocket() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    if "ixwebsocket" not in content:
        fail("cmake/EdgeTtsDependencies.cmake does not reference ixwebsocket")
    if "submodules/ixwebsocket" not in content:
        fail("cmake/EdgeTtsDependencies.cmake does not use submodules/ixwebsocket path")
    ok("cmake/EdgeTtsDependencies.cmake references ixwebsocket")


def test_edge_tts_dependencies_cmake_suppresses_warnings() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    if "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES" not in content:
        fail(
            "cmake/EdgeTtsDependencies.cmake missing INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "
            "(required to suppress third-party warnings)"
        )
    ok("cmake/EdgeTtsDependencies.cmake sets INTERFACE_SYSTEM_INCLUDE_DIRECTORIES")


def test_edge_tts_dependencies_cmake_guards_on_exists() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    # The file must guard with EXISTS so the build works without the submodule.
    if not re.search(r'if\s*\(\s*EXISTS.*ixwebsocket.*CMakeLists\.txt', content):
        fail(
            "cmake/EdgeTtsDependencies.cmake must guard ixwebsocket with "
            'if(EXISTS "…/ixwebsocket/CMakeLists.txt") for graceful degradation'
        )
    ok("cmake/EdgeTtsDependencies.cmake guards with EXISTS")


# ---------------------------------------------------------------------------
# 2. cmake/Dependencies.cmake includes EdgeTtsDependencies
# ---------------------------------------------------------------------------

def test_dependencies_cmake_includes_edge_tts_dependencies() -> None:
    path = REPO_ROOT / "cmake" / "Dependencies.cmake"
    if not path.exists():
        fail("cmake/Dependencies.cmake does not exist")
    content = read(path)
    if "EdgeTtsDependencies" not in content:
        fail(
            "cmake/Dependencies.cmake does not include(EdgeTtsDependencies). "
            "The ixwebsocket setup must be orchestrated from Dependencies.cmake."
        )
    ok("cmake/Dependencies.cmake includes EdgeTtsDependencies")


# ---------------------------------------------------------------------------
# 3. .gitmodules registers the ixwebsocket submodule
# ---------------------------------------------------------------------------

def test_gitmodules_registers_ixwebsocket() -> None:
    gitmodules = REPO_ROOT / ".gitmodules"
    if not gitmodules.exists():
        fail(".gitmodules does not exist")
    content = read(gitmodules)
    if "ixwebsocket" not in content:
        fail(".gitmodules does not register the ixwebsocket submodule")
    if "machinezone/IXWebSocket" not in content:
        fail(".gitmodules does not reference the canonical IXWebSocket GitHub URL")
    ok(".gitmodules registers ixwebsocket submodule")


# ---------------------------------------------------------------------------
# 4. CMakeLists.txt wires ixwebsocket to the communication module
# ---------------------------------------------------------------------------

def test_cmake_wires_ixwebsocket_to_communication() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)
    # Must have a conditional block that links ixwebsocket to communication.
    if not re.search(
        r'if\s*\(TARGET\s+ixwebsocket\)',
        content
    ):
        fail(
            "CMakeLists.txt missing 'if(TARGET ixwebsocket)' guard for optional linking"
        )
    if "edge_tts_communication" not in content or "ixwebsocket" not in content:
        fail("CMakeLists.txt does not link ixwebsocket to edge_tts_communication")
    ok("CMakeLists.txt conditionally links ixwebsocket to edge_tts_communication")


# ---------------------------------------------------------------------------
# 5. Public headers do not include ixwebsocket headers directly
# ---------------------------------------------------------------------------

def test_public_headers_dependency_free() -> None:
    include_root = REPO_ROOT / "include" / "edge_tts"
    if not include_root.exists():
        fail("include/edge_tts/ directory does not exist")

    violations = []
    ixws_patterns = [
        re.compile(r'#\s*include\s+[<"]ixwebsocket/', re.IGNORECASE),
        re.compile(r'#\s*include\s+[<"]IXWebSocket', re.IGNORECASE),
        re.compile(r'#\s*include\s+[<"]ix/', re.IGNORECASE),
    ]

    for header in include_root.rglob("*.hpp"):
        content = read(header)
        for pattern in ixws_patterns:
            if pattern.search(content):
                violations.append(str(header.relative_to(REPO_ROOT)))
                break

    if violations:
        fail(
            "Public headers must not include ixwebsocket headers directly. "
            "Violations:\n  " + "\n  ".join(violations)
        )
    ok(f"Public headers are ixwebsocket-free ({sum(1 for _ in include_root.rglob('*.hpp'))} headers checked)")


# ---------------------------------------------------------------------------
# 6. DEPENDENCIES.md documents ixwebsocket
# ---------------------------------------------------------------------------

def test_dependencies_md_documents_ixwebsocket() -> None:
    deps_md = REPO_ROOT / "docs" / "DEPENDENCIES.md"
    if not deps_md.exists():
        fail("docs/DEPENDENCIES.md does not exist")
    content = read(deps_md)
    if "ixwebsocket" not in content:
        fail("docs/DEPENDENCIES.md does not mention ixwebsocket")
    if "machinezone" not in content and "IXWebSocket" not in content:
        fail("docs/DEPENDENCIES.md does not name the ixwebsocket project")
    if "git submodule" not in content.lower():
        fail("docs/DEPENDENCIES.md does not include submodule initialization instructions")
    ok("docs/DEPENDENCIES.md documents ixwebsocket")


# ---------------------------------------------------------------------------
# 7. EXCLUDE_FROM_ALL is used so ixwebsocket is not always built
# ---------------------------------------------------------------------------

def test_edge_tts_dependencies_cmake_uses_exclude_from_all() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)
    if "EXCLUDE_FROM_ALL" not in content:
        fail(
            "cmake/EdgeTtsDependencies.cmake must use EXCLUDE_FROM_ALL in "
            "add_subdirectory so ixwebsocket is only built when linked"
        )
    ok("cmake/EdgeTtsDependencies.cmake uses EXCLUDE_FROM_ALL")


# ---------------------------------------------------------------------------
# 8. nlohmann/json is NOT optional — CMakeLists.txt must link it unconditionally
# ---------------------------------------------------------------------------

def test_json_is_not_optional() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)

    # nlohmann/json must be wired unconditionally to serialization and communication.
    # The wiring can be via target_link_libraries OR target_include_directories
    # (the latter is used when nlohmann types are private to implementation files).
    # We look for an unconditional reference to nlohmann_json near the relevant targets.
    has_nlohmann_ref = "nlohmann_json" in content
    if not has_nlohmann_ref:
        fail(
            "CMakeLists.txt must unconditionally reference nlohmann_json::nlohmann_json "
            "(json is required, not optional)"
        )

    # The reference must not be wrapped in an 'if(TARGET nlohmann_json::nlohmann_json)' guard.
    if re.search(r'if\s*\(\s*TARGET\s+nlohmann_json::nlohmann_json\s*\)', content):
        fail(
            "CMakeLists.txt still has 'if(TARGET nlohmann_json::nlohmann_json)' guard. "
            "nlohmann/json is now required; remove the conditional."
        )

    # Must reference nlohmann_json in the context of serialization and communication.
    # Accept either target_link_libraries or target_include_directories approaches.
    has_serial = re.search(
        r'(edge_tts_serialization|edge_tts_communication)[^;]*nlohmann_json|'
        r'nlohmann_json[^;]*(edge_tts_serialization|edge_tts_communication)|'
        r'foreach\s*\([^)]*edge_tts_serialization[^)]*edge_tts_communication[^)]*\)',
        content,
        re.DOTALL,
    )
    if not has_serial:
        fail(
            "CMakeLists.txt must unconditionally reference nlohmann_json::nlohmann_json "
            "in connection with edge_tts_serialization and edge_tts_communication"
        )

    ok("CMakeLists.txt wires nlohmann_json unconditionally to serialization and communication")


# ---------------------------------------------------------------------------
# 9. EDGE_TTS_FETCH_DEPS option is defined in cmake/ProjectOptions.cmake
# ---------------------------------------------------------------------------

def test_fetch_deps_option_exists() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    if not path.exists():
        fail("cmake/ProjectOptions.cmake does not exist")
    content = read(path)
    if "EDGE_TTS_FETCH_DEPS" not in content:
        fail(
            "cmake/ProjectOptions.cmake does not define EDGE_TTS_FETCH_DEPS option"
        )
    ok("cmake/ProjectOptions.cmake defines EDGE_TTS_FETCH_DEPS")


# ---------------------------------------------------------------------------
# 10. EDGE_TTS_REQUIRE_NETWORKING option is defined in cmake/ProjectOptions.cmake
# ---------------------------------------------------------------------------

def test_require_networking_option_exists() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    content = read(path)
    if "EDGE_TTS_REQUIRE_NETWORKING" not in content:
        fail(
            "cmake/ProjectOptions.cmake does not define EDGE_TTS_REQUIRE_NETWORKING option"
        )
    ok("cmake/ProjectOptions.cmake defines EDGE_TTS_REQUIRE_NETWORKING")


# ---------------------------------------------------------------------------
# 11. cmake/Dependencies.cmake has the correct json lookup order
# ---------------------------------------------------------------------------

def test_json_lookup_order() -> None:
    path = REPO_ROOT / "cmake" / "Dependencies.cmake"
    content = read(path)

    # Must reference the submodule path
    if "submodules/json" not in content:
        fail("cmake/Dependencies.cmake does not reference submodules/json path")

    # Must use find_package for json
    if not re.search(r'find_package\s*\(\s*nlohmann_json', content):
        fail("cmake/Dependencies.cmake does not use find_package for nlohmann_json")

    # Must use FetchContent for json
    if not re.search(r'FetchContent_Declare\s*\(\s*\n?\s*nlohmann_json', content, re.MULTILINE):
        fail("cmake/Dependencies.cmake does not use FetchContent_Declare for nlohmann_json")

    # Order check: submodule → find_package → FetchContent
    submodule_pos = content.find("submodules/json")
    find_package_pos = content.find("find_package(nlohmann_json")
    fetch_pos = content.find("FetchContent_Declare")

    if not (submodule_pos < find_package_pos < fetch_pos):
        fail(
            "cmake/Dependencies.cmake json lookup order is wrong. "
            "Expected: submodule path → find_package → FetchContent"
        )

    ok("cmake/Dependencies.cmake has correct json lookup order: submodule → find_package → FetchContent")


# ---------------------------------------------------------------------------
# 12. cmake/EdgeTtsDependencies.cmake has the correct ixwebsocket lookup order
# ---------------------------------------------------------------------------

def test_ixwebsocket_lookup_order() -> None:
    path = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    content = read(path)

    # Must reference the submodule path
    if "submodules/ixwebsocket" not in content:
        fail("cmake/EdgeTtsDependencies.cmake does not reference submodules/ixwebsocket path")

    # Must use find_package for ixwebsocket
    if not re.search(r'find_package\s*\(\s*ixwebsocket', content):
        fail("cmake/EdgeTtsDependencies.cmake does not use find_package for ixwebsocket")

    # Must use FetchContent for ixwebsocket
    if not re.search(r'FetchContent_Declare\s*\(\s*\n?\s*ixwebsocket', content, re.MULTILINE):
        fail("cmake/EdgeTtsDependencies.cmake does not use FetchContent_Declare for ixwebsocket")

    # Order check: submodule → find_package → FetchContent
    submodule_pos = content.find("submodules/ixwebsocket")
    find_package_pos = content.find("find_package(ixwebsocket")
    fetch_pos_match = re.search(r'FetchContent_Declare\s*\(\s*\n?\s*ixwebsocket', content, re.MULTILINE)
    fetch_pos = fetch_pos_match.start() if fetch_pos_match else -1

    if not (submodule_pos < find_package_pos < fetch_pos):
        fail(
            "cmake/EdgeTtsDependencies.cmake ixwebsocket lookup order is wrong. "
            "Expected: submodule path → find_package → FetchContent"
        )

    ok("cmake/EdgeTtsDependencies.cmake has correct ixwebsocket lookup order: submodule → find_package → FetchContent")


# ---------------------------------------------------------------------------
# 13. cmake/Dependencies.cmake has a FATAL_ERROR for json not found
# ---------------------------------------------------------------------------

def test_fatal_error_when_json_unavailable() -> None:
    path = REPO_ROOT / "cmake" / "Dependencies.cmake"
    content = read(path)
    if not re.search(r'message\s*\(\s*FATAL_ERROR', content):
        fail(
            "cmake/Dependencies.cmake must have a message(FATAL_ERROR ...) "
            "for when nlohmann/json cannot be found"
        )
    ok("cmake/Dependencies.cmake has FATAL_ERROR for missing nlohmann/json")


# ---------------------------------------------------------------------------
# 14. FATAL_ERROR is gated on EDGE_TTS_REQUIRE_NETWORKING for missing ixwebsocket
# ---------------------------------------------------------------------------

def test_fatal_error_when_networking_required_but_missing() -> None:
    deps_cmake = REPO_ROOT / "cmake" / "EdgeTtsDependencies.cmake"
    root_cmake = REPO_ROOT / "CMakeLists.txt"

    deps_content = read(deps_cmake)
    root_content = read(root_cmake)
    combined = deps_content + root_content

    # Must have FATAL_ERROR related to REQUIRE_NETWORKING
    if not re.search(r'EDGE_TTS_REQUIRE_NETWORKING', combined):
        fail(
            "Neither cmake/EdgeTtsDependencies.cmake nor CMakeLists.txt references "
            "EDGE_TTS_REQUIRE_NETWORKING"
        )

    has_fatal = re.search(r'message\s*\(\s*FATAL_ERROR', combined)
    if not has_fatal:
        fail(
            "No message(FATAL_ERROR ...) gated on EDGE_TTS_REQUIRE_NETWORKING found "
            "in cmake/EdgeTtsDependencies.cmake or CMakeLists.txt"
        )

    ok("FATAL_ERROR for missing ixwebsocket is gated on EDGE_TTS_REQUIRE_NETWORKING")


# ---------------------------------------------------------------------------
# 15. CMakePresets.json exists with the four required presets
# ---------------------------------------------------------------------------

def test_cmake_presets_exist() -> None:
    import json as _json

    presets_path = REPO_ROOT / "CMakePresets.json"
    if not presets_path.exists():
        fail("CMakePresets.json does not exist")

    try:
        data = _json.loads(presets_path.read_text(encoding="utf-8"))
    except _json.JSONDecodeError as exc:
        fail(f"CMakePresets.json is not valid JSON: {exc}")

    configure_names = {p["name"] for p in data.get("configurePresets", [])}
    required = {"developer", "offline-system", "offline-no-networking", "archive-verify"}
    missing = required - configure_names
    if missing:
        fail(
            f"CMakePresets.json is missing required configurePresets: {sorted(missing)}\n"
            f"  Required: {sorted(required)}\n"
            f"  Present:  {sorted(configure_names)}"
        )

    # developer preset must have FETCH_DEPS=ON
    dev = next(p for p in data["configurePresets"] if p["name"] == "developer")
    fetch = dev.get("cacheVariables", {}).get("EDGE_TTS_FETCH_DEPS", {})
    fetch_val = fetch.get("value", fetch) if isinstance(fetch, dict) else fetch
    if str(fetch_val).upper() not in ("ON", "TRUE", "1", "YES"):
        fail("CMakePresets.json 'developer' preset must set EDGE_TTS_FETCH_DEPS=ON")

    # offline presets must have FETCH_DEPS=OFF
    for preset_name in ("offline-system", "offline-no-networking", "archive-verify"):
        p = next(p for p in data["configurePresets"] if p["name"] == preset_name)
        fetch = p.get("cacheVariables", {}).get("EDGE_TTS_FETCH_DEPS", {})
        fetch_val = fetch.get("value", fetch) if isinstance(fetch, dict) else fetch
        if str(fetch_val).upper() not in ("OFF", "FALSE", "0", "NO"):
            fail(
                f"CMakePresets.json '{preset_name}' preset must set EDGE_TTS_FETCH_DEPS=OFF"
            )

    ok(f"CMakePresets.json has all required presets: {sorted(required)}")


# ---------------------------------------------------------------------------
# 16. docs/RELEASE.md exists and covers required topics
# ---------------------------------------------------------------------------

def test_release_md_exists() -> None:
    release_md = REPO_ROOT / "docs" / "RELEASE.md"
    if not release_md.exists():
        fail("docs/RELEASE.md does not exist")
    content = read(release_md)

    required_topics = [
        ("make_release_archive", "make_release_archive.sh script"),
        ("archive-verify", "archive-verify preset"),
        ("EDGE_TTS_FETCH_DEPS", "EDGE_TTS_FETCH_DEPS option"),
        ("EDGE_TTS_REQUIRE_NETWORKING", "EDGE_TTS_REQUIRE_NETWORKING option"),
        ("submodule", "submodule initialization instructions"),
        ("FATAL_ERROR", "configure-time failure documentation"),
    ]
    for keyword, description in required_topics:
        if keyword not in content:
            fail(f"docs/RELEASE.md does not cover {description} (missing: '{keyword}')")

    ok("docs/RELEASE.md exists and covers all required topics")


# ---------------------------------------------------------------------------
# 17. Production apps do not reference FakeWebSocketClient or FakeHttpClient
# ---------------------------------------------------------------------------

def test_production_apps_no_fake_networking() -> None:
    apps_dir = REPO_ROOT / "apps"
    if not apps_dir.exists():
        fail("apps/ directory does not exist")

    fake_patterns = [
        re.compile(r'FakeWebSocketClient', re.IGNORECASE),
        re.compile(r'FakeHttpClient', re.IGNORECASE),
    ]
    violations = []
    for src_file in apps_dir.rglob("*.cpp"):
        content = read(src_file)
        for pattern in fake_patterns:
            if pattern.search(content):
                violations.append(str(src_file.relative_to(REPO_ROOT)))
                break
    for src_file in apps_dir.rglob("*.hpp"):
        content = read(src_file)
        for pattern in fake_patterns:
            if pattern.search(content):
                violations.append(str(src_file.relative_to(REPO_ROOT)))
                break

    if violations:
        fail(
            "Production app sources reference fake networking classes. "
            "Fake clients must never appear in apps/.\nViolations:\n  "
            + "\n  ".join(violations)
        )
    ok("Production apps do not reference FakeWebSocketClient or FakeHttpClient")


# ---------------------------------------------------------------------------
# 18. Guard in CMakeLists.txt prevents apps from building against stub networking
# ---------------------------------------------------------------------------

def test_apps_stub_networking_guard() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)

    # The guard must check all three conditions together
    if not re.search(
        r'EDGE_TTS_BUILD_APPS.*EDGE_TTS_REQUIRE_NETWORKING.*ixwebsocket|'
        r'EDGE_TTS_REQUIRE_NETWORKING.*EDGE_TTS_BUILD_APPS.*ixwebsocket',
        content,
        re.DOTALL,
    ):
        fail(
            "CMakeLists.txt must have a guard that combines EDGE_TTS_BUILD_APPS, "
            "EDGE_TTS_REQUIRE_NETWORKING, and ixwebsocket availability to prevent "
            "silently building apps against stub networking"
        )

    # Must be a FATAL_ERROR, not just a warning
    guard_region = re.search(
        r'if\s*\(.*EDGE_TTS_BUILD_APPS.*\).*?endif\s*\(\)',
        content, re.DOTALL
    )
    # Simpler check: both the relevant variables and FATAL_ERROR appear in the file
    if "EDGE_TTS_BUILD_APPS" not in content or "FATAL_ERROR" not in content:
        fail(
            "CMakeLists.txt is missing the FATAL_ERROR guard for apps built without ixwebsocket"
        )

    ok("CMakeLists.txt has FATAL_ERROR guard preventing apps from using stub networking")


# ---------------------------------------------------------------------------
# 19. EDGE_TTS_FETCH_DEPS defaults to OFF in cmake/ProjectOptions.cmake
# ---------------------------------------------------------------------------

def test_fetch_deps_defaults_to_off() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    content = read(path)
    # The option() call for EDGE_TTS_FETCH_DEPS must end with OFF
    match = re.search(
        r'option\s*\(\s*EDGE_TTS_FETCH_DEPS[^)]*\)',
        content,
        re.DOTALL,
    )
    if not match:
        fail("cmake/ProjectOptions.cmake has no option(EDGE_TTS_FETCH_DEPS ...) call")
    option_text = match.group(0)
    if not re.search(r'\bOFF\b', option_text):
        fail(
            "cmake/ProjectOptions.cmake: EDGE_TTS_FETCH_DEPS must default to OFF so "
            "missing deps fail with a clear configure-time message instead of a "
            "confusing network/git error"
        )
    ok("cmake/ProjectOptions.cmake: EDGE_TTS_FETCH_DEPS defaults to OFF")


# ---------------------------------------------------------------------------
# Functional cmake tests (require cmake ≥ 3.24 on PATH)
# ---------------------------------------------------------------------------

def _cmake_available() -> bool:
    return shutil.which("cmake") is not None


def _create_minimal_cmake_project(
    tmp: pathlib.Path,
    have_json_submodule: bool = False,
    have_ixwebsocket_submodule: bool = False,
) -> None:
    """Write a minimal CMakeLists.txt + cmake/ layout in tmp for dependency tests."""
    cmake_dst = tmp / "cmake"
    cmake_dst.mkdir()
    for name in ("ProjectOptions.cmake", "Dependencies.cmake", "EdgeTtsDependencies.cmake"):
        shutil.copy2(REPO_ROOT / "cmake" / name, cmake_dst / name)

    (tmp / "submodules").mkdir()

    # Fake json submodule: provides nlohmann_json::nlohmann_json target
    if have_json_submodule:
        json_dir = tmp / "submodules" / "json"
        json_dir.mkdir()
        (json_dir / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.24)\n"
            "project(nlohmann_json)\n"
            "add_library(nlohmann_json INTERFACE)\n"
            "add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)\n"
        )

    # Fake ixwebsocket submodule: provides ixwebsocket target
    if have_ixwebsocket_submodule:
        ix_dir = tmp / "submodules" / "ixwebsocket"
        ix_dir.mkdir()
        (ix_dir / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.24)\n"
            "project(IXWebSocket)\n"
            "add_library(ixwebsocket INTERFACE)\n"
        )

    (tmp / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.24)\n"
        "project(test_dep_resolution CXX)\n"
        'set(EDGE_TTS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")\n'
        'set(EDGE_TTS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")\n'
        'list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")\n'
        "include(ProjectOptions)\n"
        "include(Dependencies)\n"
        "edge_tts_setup_options()\n"
        "edge_tts_setup_dependencies()\n"
    )


def _run_cmake(src: pathlib.Path, build: pathlib.Path, extra_args: list) -> subprocess.CompletedProcess:
    cmd = [
        "cmake",
        "-S", str(src),
        "-B", str(build),
        "-G", "Unix Makefiles",
    ] + extra_args
    return subprocess.run(cmd, capture_output=True, text=True, timeout=120)


# ---------------------------------------------------------------------------
# 20. Functional: json missing + FETCH_DEPS=OFF → configure FATAL_ERROR
# ---------------------------------------------------------------------------

def test_configure_fails_clearly_when_json_missing() -> None:
    if not _cmake_available():
        print("  SKIP test_configure_fails_clearly_when_json_missing (cmake not on PATH)")
        return

    with tempfile.TemporaryDirectory(prefix="edge_tts_test_") as tmp_str:
        tmp = pathlib.Path(tmp_str)
        _create_minimal_cmake_project(tmp, have_json_submodule=False, have_ixwebsocket_submodule=False)
        build = tmp / "build"

        result = _run_cmake(tmp, build, [
            "-DEDGE_TTS_FETCH_DEPS=OFF",
            # Prevent find_package from locating system nlohmann_json
            "-DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=TRUE",
            "-DEDGE_TTS_REQUIRE_NETWORKING=OFF",
        ])

        if result.returncode == 0:
            fail(
                "configure succeeded but should have failed: "
                "nlohmann/json was unavailable and EDGE_TTS_FETCH_DEPS=OFF"
            )

        combined = result.stdout + result.stderr
        if "nlohmann" not in combined and "json" not in combined.lower():
            fail(
                "configure failed but the error does not mention nlohmann/json.\n"
                f"stdout: {result.stdout[:500]}\nstderr: {result.stderr[:500]}"
            )

        if "FATAL_ERROR" not in combined and "CMake Error" not in combined:
            fail(
                "configure failed but output does not contain a CMake FATAL_ERROR.\n"
                f"stdout: {result.stdout[:500]}\nstderr: {result.stderr[:500]}"
            )

    ok("configure fails with a clear json-specific message when nlohmann/json is unavailable and FETCH_DEPS=OFF")


# ---------------------------------------------------------------------------
# 21. Functional: ixwebsocket missing + REQUIRE_NETWORKING=ON → FATAL_ERROR
# ---------------------------------------------------------------------------

def test_configure_fails_when_ixwebsocket_missing_and_networking_required() -> None:
    if not _cmake_available():
        print("  SKIP test_configure_fails_when_ixwebsocket_missing_and_networking_required (cmake not on PATH)")
        return

    with tempfile.TemporaryDirectory(prefix="edge_tts_test_") as tmp_str:
        tmp = pathlib.Path(tmp_str)
        # Provide json (so configure gets past that check), but not ixwebsocket
        _create_minimal_cmake_project(tmp, have_json_submodule=True, have_ixwebsocket_submodule=False)
        build = tmp / "build"

        result = _run_cmake(tmp, build, [
            "-DEDGE_TTS_FETCH_DEPS=OFF",
            "-DEDGE_TTS_REQUIRE_NETWORKING=ON",
            "-DCMAKE_DISABLE_FIND_PACKAGE_ixwebsocket=TRUE",
        ])

        if result.returncode == 0:
            fail(
                "configure succeeded but should have failed: "
                "ixwebsocket unavailable and EDGE_TTS_REQUIRE_NETWORKING=ON"
            )

        combined = result.stdout + result.stderr
        if "ixwebsocket" not in combined.lower():
            fail(
                "configure failed but error does not mention ixwebsocket.\n"
                f"stdout: {result.stdout[:500]}\nstderr: {result.stderr[:500]}"
            )

    ok("configure fails with a clear ixwebsocket-specific error when REQUIRE_NETWORKING=ON and ixwebsocket is missing")


# ---------------------------------------------------------------------------
# 22. Functional: ixwebsocket missing + REQUIRE_NETWORKING=OFF → succeeds
# ---------------------------------------------------------------------------

def test_configure_succeeds_without_ixwebsocket_when_networking_not_required() -> None:
    if not _cmake_available():
        print("  SKIP test_configure_succeeds_without_ixwebsocket_when_networking_not_required (cmake not on PATH)")
        return

    with tempfile.TemporaryDirectory(prefix="edge_tts_test_") as tmp_str:
        tmp = pathlib.Path(tmp_str)
        # Provide json only; no ixwebsocket
        _create_minimal_cmake_project(tmp, have_json_submodule=True, have_ixwebsocket_submodule=False)
        build = tmp / "build"

        result = _run_cmake(tmp, build, [
            "-DEDGE_TTS_FETCH_DEPS=OFF",
            "-DEDGE_TTS_REQUIRE_NETWORKING=OFF",
            "-DCMAKE_DISABLE_FIND_PACKAGE_ixwebsocket=TRUE",
        ])

        if result.returncode != 0:
            fail(
                "configure failed but should have succeeded: "
                "REQUIRE_NETWORKING=OFF so missing ixwebsocket is acceptable.\n"
                f"stdout: {result.stdout[:800]}\nstderr: {result.stderr[:800]}"
            )

        # Must not have built with a FATAL_ERROR
        combined = result.stdout + result.stderr
        if re.search(r'CMake Error|FATAL_ERROR', combined):
            fail(
                "configure reported a CMake error even with REQUIRE_NETWORKING=OFF.\n"
                f"stdout: {result.stdout[:500]}\nstderr: {result.stderr[:500]}"
            )

    ok("configure succeeds without ixwebsocket when REQUIRE_NETWORKING=OFF (stub networking path)")


# ---------------------------------------------------------------------------
# 23. Release archive smoke test: git archive → configure fails clearly
# ---------------------------------------------------------------------------

def test_release_archive_smoke() -> None:
    """
    Simulate a release archive (no submodule contents) and verify configure
    fails with a clear dependency error when FETCH_DEPS=OFF, not with an
    ambiguous compile-time error or silent success.
    """
    if not _cmake_available():
        print("  SKIP test_release_archive_smoke (cmake not on PATH)")
        return
    if not shutil.which("git"):
        print("  SKIP test_release_archive_smoke (git not on PATH)")
        return

    with tempfile.TemporaryDirectory(prefix="edge_tts_archive_") as tmp_str:
        tmp = pathlib.Path(tmp_str)
        prefix = "edge-tts-cpp-smoke"

        # Produce a git archive of the superproject (submodules not included)
        git_archive = subprocess.run(
            ["git", "archive", f"--prefix={prefix}/", "--format=tar", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            timeout=30,
        )
        if git_archive.returncode != 0:
            print(f"  SKIP test_release_archive_smoke (git archive failed: {git_archive.stderr.decode()[:200]})")
            return

        # Extract into tmp — produces tmp/edge-tts-cpp-smoke/
        extract = subprocess.run(
            ["tar", "x", "-C", str(tmp)],
            input=git_archive.stdout,
            capture_output=True,
            timeout=30,
        )
        if extract.returncode != 0:
            print("  SKIP test_release_archive_smoke (tar extraction failed)")
            return

        archive_src = tmp / prefix
        if not (archive_src / "CMakeLists.txt").exists():
            print("  SKIP test_release_archive_smoke (git archive layout unexpected)")
            return

        # Confirm: submodule dirs exist but are empty (no CMakeLists.txt)
        json_cmakelists = archive_src / "submodules" / "json" / "CMakeLists.txt"
        if json_cmakelists.exists():
            # The archive unexpectedly has the submodule — skip gracefully
            print("  SKIP test_release_archive_smoke (git archive contained submodule content)")
            return

        build = tmp / "build"

        # Configure using the archive-verify-equivalent settings
        result = _run_cmake(archive_src, build, [
            "-DEDGE_TTS_FETCH_DEPS=OFF",
            "-DEDGE_TTS_BUILD_APPS=OFF",
            "-DEDGE_TTS_REQUIRE_NETWORKING=OFF",
            "-DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=TRUE",
        ])

        if result.returncode == 0:
            fail(
                "Release archive configure succeeded without submodule contents and "
                "FETCH_DEPS=OFF — should have failed with a clear dependency error."
            )

        combined = result.stdout + result.stderr
        # The error must name the missing dependency explicitly
        if "nlohmann" not in combined and "json" not in combined.lower():
            fail(
                "Release archive configure failed but the error does not name the missing "
                "dependency (expected 'nlohmann' or 'json').\n"
                f"stderr: {result.stderr[:600]}"
            )

        # No root-level generated CMake artifacts should exist (archive is clean)
        for artifact in ("CMakeCache.txt", "CMakeFiles"):
            if (archive_src / artifact).exists():
                fail(
                    f"Release archive contains generated CMake artifact '{artifact}' "
                    "— source tree is not clean."
                )

    ok("Release archive smoke test: configure fails with a clear dependency-specific error when submodules absent and FETCH_DEPS=OFF")


# ---------------------------------------------------------------------------
# 24. EDGE_TTS_BUILD_PLAYBACK_APP option is defined in cmake/ProjectOptions.cmake
# ---------------------------------------------------------------------------

def test_build_playback_app_option_exists() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    content = read(path)
    if "EDGE_TTS_BUILD_PLAYBACK_APP" not in content:
        fail(
            "cmake/ProjectOptions.cmake does not define EDGE_TTS_BUILD_PLAYBACK_APP option. "
            "This option controls whether the edge-playback CLI app is built and must "
            "default OFF on Windows."
        )
    ok("cmake/ProjectOptions.cmake defines EDGE_TTS_BUILD_PLAYBACK_APP")


def test_build_playback_app_defaults_off_on_windows() -> None:
    path = REPO_ROOT / "cmake" / "ProjectOptions.cmake"
    content = read(path)
    # Must have a WIN32 check that sets it to OFF.
    if not re.search(r'WIN32', content):
        fail(
            "cmake/ProjectOptions.cmake does not check WIN32 when setting "
            "EDGE_TTS_BUILD_PLAYBACK_APP — the option must default OFF on Windows."
        )
    ok("cmake/ProjectOptions.cmake guards EDGE_TTS_BUILD_PLAYBACK_APP default with WIN32 check")


def test_build_playback_app_fatal_error_on_windows() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)
    # Must have a FATAL_ERROR that fires when EDGE_TTS_BUILD_PLAYBACK_APP=ON on Windows.
    if "EDGE_TTS_BUILD_PLAYBACK_APP" not in content:
        fail(
            "CMakeLists.txt does not reference EDGE_TTS_BUILD_PLAYBACK_APP. "
            "The root CMakeLists.txt must gate edge-playback on this option and "
            "emit FATAL_ERROR when it is ON on Windows."
        )
    if not re.search(r'WIN32', content):
        fail(
            "CMakeLists.txt does not check WIN32 for EDGE_TTS_BUILD_PLAYBACK_APP guard."
        )
    ok("CMakeLists.txt has WIN32 guard for EDGE_TTS_BUILD_PLAYBACK_APP")


# ---------------------------------------------------------------------------
# 25. ProcessRunner.cpp is conditionally compiled (not on WIN32)
# ---------------------------------------------------------------------------

def test_process_runner_conditionally_compiled() -> None:
    root_cmake = REPO_ROOT / "CMakeLists.txt"
    content = read(root_cmake)
    # Must have a conditional that excludes ProcessRunner.cpp on Windows.
    if "ProcessRunner.cpp" not in content:
        fail(
            "CMakeLists.txt does not reference ProcessRunner.cpp — "
            "the media module must include it conditionally (non-WIN32 only)."
        )
    # The conditional must guard it with NOT WIN32 or similar.
    if not re.search(r'NOT\s+WIN32', content):
        fail(
            "CMakeLists.txt does not use 'NOT WIN32' to guard ProcessRunner.cpp. "
            "ProcessRunner.cpp must only be compiled on POSIX platforms."
        )
    ok("CMakeLists.txt compiles ProcessRunner.cpp conditionally (NOT WIN32)")


# ---------------------------------------------------------------------------
# 26. ProcessRunner.hpp guards the concrete class with #ifndef _WIN32
# ---------------------------------------------------------------------------

def test_process_runner_hpp_guards_concrete_class() -> None:
    header = REPO_ROOT / "modules" / "media" / "include" / "media" / "ProcessRunner.hpp"
    if not header.exists():
        fail("modules/media/include/media/ProcessRunner.hpp does not exist")
    content = read(header)

    # The concrete class declaration must be wrapped in #ifndef _WIN32 so that
    # including the header on Windows does not expose a class with no implementation.
    if not re.search(r'#ifndef\s+_WIN32', content):
        fail(
            "ProcessRunner.hpp must guard the concrete ProcessRunner class with "
            "#ifndef _WIN32. On Windows, including this header should only give "
            "IProcessRunner, ProcessCommand, and ProcessResult — not ProcessRunner."
        )

    # The closing #endif must follow the class definition.
    ifndef_pos = content.find("#ifndef _WIN32")
    class_pos = content.find("class ProcessRunner", ifndef_pos)
    endif_pos = content.find("#endif", class_pos if class_pos >= 0 else ifndef_pos)

    if ifndef_pos < 0 or class_pos < 0 or endif_pos < 0:
        fail(
            "ProcessRunner.hpp: could not locate #ifndef _WIN32 / class ProcessRunner / "
            "#endif in the expected order. The concrete class must be inside the guard."
        )
    if not (ifndef_pos < class_pos < endif_pos):
        fail(
            "ProcessRunner.hpp: #ifndef _WIN32 does not wrap the ProcessRunner class "
            "declaration. Expected order: #ifndef _WIN32 → class ProcessRunner → #endif."
        )

    ok("ProcessRunner.hpp guards the concrete ProcessRunner class with #ifndef _WIN32")


# ---------------------------------------------------------------------------
# 27. docs/HIGH_LEVEL_DESIGN.md contains a platform support matrix
# ---------------------------------------------------------------------------

def test_high_level_design_has_platform_matrix() -> None:
    hld = REPO_ROOT / "docs" / "HIGH_LEVEL_DESIGN.md"
    if not hld.exists():
        fail("docs/HIGH_LEVEL_DESIGN.md does not exist")
    content = read(hld)

    # Must have a "Platform support" section.
    if not re.search(r'##\s+Platform\s+support', content, re.IGNORECASE):
        fail(
            "docs/HIGH_LEVEL_DESIGN.md does not have a '## Platform support' section. "
            "Add a platform support matrix so Windows users know what builds and what does not."
        )

    # Must mention Windows, Linux/macOS, and ProcessRunner.
    for keyword in ("Windows", "Linux", "ProcessRunner"):
        if keyword not in content:
            fail(
                f"docs/HIGH_LEVEL_DESIGN.md platform support section must mention '{keyword}'"
            )

    # Must mention the FATAL_ERROR behavior or the configure-time error so users
    # know exactly what happens when they try EDGE_TTS_BUILD_PLAYBACK_APP=ON on Windows.
    if "FATAL_ERROR" not in content and "EDGE_TTS_BUILD_PLAYBACK_APP" not in content:
        fail(
            "docs/HIGH_LEVEL_DESIGN.md must document the EDGE_TTS_BUILD_PLAYBACK_APP "
            "configure-time FATAL_ERROR for Windows users."
        )

    ok("docs/HIGH_LEVEL_DESIGN.md has a Platform support section covering Windows/Linux/ProcessRunner")


# ---------------------------------------------------------------------------
# 28. docs/CONSUMING.md documents Windows platform support
# ---------------------------------------------------------------------------

def test_consuming_md_documents_windows_build() -> None:
    consuming = REPO_ROOT / "docs" / "CONSUMING.md"
    if not consuming.exists():
        fail("docs/CONSUMING.md does not exist")
    content = read(consuming)

    if "Windows" not in content:
        fail(
            "docs/CONSUMING.md does not mention Windows. "
            "Library consumers on Windows need to know what is supported."
        )

    # Must note that the library is supported but edge-playback is not.
    if "IProcessRunner" not in content and "ProcessRunner" not in content:
        fail(
            "docs/CONSUMING.md does not mention IProcessRunner / ProcessRunner. "
            "Windows users need guidance on how to extend playback support."
        )

    ok("docs/CONSUMING.md documents Windows platform support and IProcessRunner extension point")


# ---------------------------------------------------------------------------
# 29. docs/HIGH_LEVEL_DESIGN.md has a comprehensive error handling reference
# ---------------------------------------------------------------------------

def test_high_level_design_has_error_handling_reference() -> None:
    hld = REPO_ROOT / "docs" / "HIGH_LEVEL_DESIGN.md"
    if not hld.exists():
        fail("docs/HIGH_LEVEL_DESIGN.md does not exist")
    content = read(hld)

    # Must have an "Error handling" section.
    if not re.search(r'##\s+Error\s+handling', content, re.IGNORECASE):
        fail(
            "docs/HIGH_LEVEL_DESIGN.md does not have an '## Error handling' section. "
            "Consumers need a reference for how to handle each ErrorCode."
        )

    # Must document all 13 ErrorCode values.
    required_codes = [
        "invalid_argument",
        "invalid_state",
        "io_error",
        "network_error",
        "protocol_error",
        "parse_error",
        "timeout",
        "unsupported",
        "external_process_failed",
        "service_error",
        "drm_error",
        "cancelled",
    ]
    for code in required_codes:
        if code not in content:
            fail(
                f"docs/HIGH_LEVEL_DESIGN.md error handling section is missing '{code}'. "
                "All ErrorCode values must be documented."
            )

    # Must document the context field policy (security).
    if "context" not in content.lower() or "redact" not in content.lower():
        fail(
            "docs/HIGH_LEVEL_DESIGN.md must document the context field policy, "
            "including credential redaction for proxy URLs."
        )

    ok("docs/HIGH_LEVEL_DESIGN.md has a complete error handling reference covering all 12 ErrorCode values")


def test_release_readiness_documents_subtitle_timing() -> None:
    """RELEASE_READINESS.md Known limitations must mention subtitle timing approximation."""
    rr = REPO_ROOT / "docs" / "RELEASE_READINESS.md"
    if not rr.exists():
        fail("docs/RELEASE_READINESS.md not found")
    content = rr.read_text(encoding="utf-8").lower()
    if "subtitle timing" not in content:
        fail(
            "docs/RELEASE_READINESS.md Known limitations must document the subtitle "
            "timing approximation (48 kbps assumption for multi-chunk offset computation)."
        )
    ok("docs/RELEASE_READINESS.md documents subtitle timing approximation as a known limitation")


def test_consuming_md_documents_subtitle_timing() -> None:
    """docs/CONSUMING.md must warn consumers about the subtitle timing approximation."""
    consuming = REPO_ROOT / "docs" / "CONSUMING.md"
    if not consuming.exists():
        fail("docs/CONSUMING.md not found")
    content = consuming.read_text(encoding="utf-8").lower()
    if "subtitle timing" not in content:
        fail(
            "docs/CONSUMING.md must document the subtitle timing approximation "
            "for multi-chunk text so library consumers are aware of the limitation."
        )
    ok("docs/CONSUMING.md documents subtitle timing approximation for multi-chunk text")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_edge_tts_dependencies_cmake_exists,
        test_edge_tts_dependencies_cmake_references_ixwebsocket,
        test_edge_tts_dependencies_cmake_suppresses_warnings,
        test_edge_tts_dependencies_cmake_guards_on_exists,
        test_dependencies_cmake_includes_edge_tts_dependencies,
        test_gitmodules_registers_ixwebsocket,
        test_cmake_wires_ixwebsocket_to_communication,
        test_public_headers_dependency_free,
        test_dependencies_md_documents_ixwebsocket,
        test_edge_tts_dependencies_cmake_uses_exclude_from_all,
        test_json_is_not_optional,
        test_fetch_deps_option_exists,
        test_require_networking_option_exists,
        test_json_lookup_order,
        test_ixwebsocket_lookup_order,
        test_fatal_error_when_json_unavailable,
        test_fatal_error_when_networking_required_but_missing,
        # New tests
        test_cmake_presets_exist,
        test_release_md_exists,
        test_production_apps_no_fake_networking,
        test_apps_stub_networking_guard,
        test_fetch_deps_defaults_to_off,
        test_configure_fails_clearly_when_json_missing,
        test_configure_fails_when_ixwebsocket_missing_and_networking_required,
        test_configure_succeeds_without_ixwebsocket_when_networking_not_required,
        test_release_archive_smoke,
        # Platform support tests
        test_build_playback_app_option_exists,
        test_build_playback_app_defaults_off_on_windows,
        test_build_playback_app_fatal_error_on_windows,
        test_process_runner_conditionally_compiled,
        test_process_runner_hpp_guards_concrete_class,
        test_high_level_design_has_platform_matrix,
        test_consuming_md_documents_windows_build,
        test_high_level_design_has_error_handling_reference,
        # Documentation completeness tests
        test_release_readiness_documents_subtitle_timing,
        test_consuming_md_documents_subtitle_timing,
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} dependency config checks passed.")


if __name__ == "__main__":
    main()
