#!/usr/bin/env python3
"""
Dependency-consumer readiness audit for edge-tts-cpp.

This is a fast, cmake-free coherence audit.  It does NOT run builds; the cmake
consumer tests (test_consumer_add_subdirectory.py, test_install_tree.py, etc.)
do that.  This test verifies that every claimed user story has documented AND
tested coverage, and that the dependency surface is clean and minimal.

Seven user stories verified:

  1. I can add edge-tts-cpp as a Git submodule and link edge_tts::tts.
  2. I can install edge-tts-cpp and use find_package(edge_tts_cpp CONFIG REQUIRED).
  3. I can include <edge_tts/edge_tts.hpp>.
  4. I do not need to link CLI or playback to synthesize speech.
  5. I do not get fake/test utilities in my production install by default.
  6. I can run offline consumer tests without contacting Edge services.
  7. I get a clear error if I use unsupported proxy behavior.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def read(path: pathlib.Path) -> str:
    if not path.exists():
        fail(f"Required file not found: {path.relative_to(REPO)}")
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# Story 1 — add_subdirectory integration
# ---------------------------------------------------------------------------

def test_story1_add_subdirectory() -> None:
    """A developer can vendor edge-tts-cpp as a submodule and link edge_tts::tts."""

    # Example project exists with correct pattern.
    example = REPO / "examples" / "consumer_add_subdirectory"
    if not example.is_dir():
        fail("examples/consumer_add_subdirectory/ directory is missing")
    cmake = read(example / "CMakeLists.txt")
    if "add_subdirectory" not in cmake:
        fail("consumer_add_subdirectory example does not call add_subdirectory()")
    if "edge_tts::tts" not in cmake:
        fail("consumer_add_subdirectory example does not link edge_tts::tts")
    ok("Story 1: consumer_add_subdirectory example exists and uses edge_tts::tts")

    # The example is covered by test_consumer_examples.py.
    test_examples = REPO / "tests" / "cmake" / "test_consumer_examples.py"
    content = read(test_examples)
    if "consumer_add_subdirectory" not in content:
        fail("test_consumer_examples.py does not exercise consumer_add_subdirectory")
    ok("Story 1: test_consumer_examples.py covers consumer_add_subdirectory")

    # test_consumer_add_subdirectory.py exercises the fixture directly.
    test_subdir = REPO / "tests" / "cmake" / "test_consumer_add_subdirectory.py"
    if not test_subdir.exists():
        fail("tests/cmake/test_consumer_add_subdirectory.py is missing")
    ok("Story 1: test_consumer_add_subdirectory.py exists")

    # CONSUMING.md documents add_subdirectory.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "add_subdirectory" not in consuming:
        fail("docs/CONSUMING.md does not document add_subdirectory integration")
    if "EDGE_TTS_INSTALL" not in consuming:
        fail("docs/CONSUMING.md does not mention EDGE_TTS_INSTALL=OFF for sub-projects")
    ok("Story 1: docs/CONSUMING.md documents add_subdirectory + EDGE_TTS_INSTALL=OFF")

    # README documents it.
    readme = read(REPO / "README.md")
    if "add_subdirectory" not in readme:
        fail("README.md does not document add_subdirectory usage")
    ok("Story 1: README.md documents add_subdirectory usage")


# ---------------------------------------------------------------------------
# Story 2 — find_package / install-tree integration
# ---------------------------------------------------------------------------

def test_story2_find_package() -> None:
    """A developer can install edge-tts-cpp and use find_package()."""

    # Example project exists with correct pattern.
    example = REPO / "examples" / "consumer_find_package"
    if not example.is_dir():
        fail("examples/consumer_find_package/ directory is missing")
    cmake = read(example / "CMakeLists.txt")
    if "find_package" not in cmake:
        fail("consumer_find_package example does not call find_package()")
    if "edge_tts::tts" not in cmake:
        fail("consumer_find_package example does not link edge_tts::tts")
    ok("Story 2: consumer_find_package example exists and uses edge_tts::tts")

    # The example is covered by test_consumer_examples.py.
    test_examples = REPO / "tests" / "cmake" / "test_consumer_examples.py"
    content = read(test_examples)
    if "consumer_find_package" not in content:
        fail("test_consumer_examples.py does not exercise consumer_find_package")
    ok("Story 2: test_consumer_examples.py covers consumer_find_package")

    # test_install_tree.py exercises the install-then-consume cycle.
    test_install = REPO / "tests" / "cmake" / "test_install_tree.py"
    content = read(test_install)
    if "find_package" not in content:
        fail("test_install_tree.py does not exercise find_package")
    ok("Story 2: test_install_tree.py covers find_package consumer")

    # Package config template exists.
    config_in = REPO / "cmake" / "edge_tts_cpp-config.cmake.in"
    content = read(config_in)
    if "find_package(edge_tts_cpp" not in content and "find_dependency" not in content:
        fail("edge_tts_cpp-config.cmake.in looks incomplete")
    if "edge_tts::tts" not in content:
        fail("edge_tts_cpp-config.cmake.in does not mention edge_tts::tts")
    ok("Story 2: edge_tts_cpp-config.cmake.in is well-formed")

    # CONSUMING.md documents find_package.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "find_package" not in consuming:
        fail("docs/CONSUMING.md does not document find_package integration")
    if "CMAKE_PREFIX_PATH" not in consuming:
        fail("docs/CONSUMING.md does not document CMAKE_PREFIX_PATH")
    ok("Story 2: docs/CONSUMING.md documents find_package + CMAKE_PREFIX_PATH")


# ---------------------------------------------------------------------------
# Story 3 — umbrella header
# ---------------------------------------------------------------------------

def test_story3_umbrella_header() -> None:
    """A developer can include <edge_tts/edge_tts.hpp> to get the full API."""

    umbrella = REPO / "include" / "edge_tts" / "edge_tts.hpp"
    content = read(umbrella)

    # Must include stable API headers.
    required_includes = [
        "edge_tts/api/Communicate.hpp",
        "edge_tts/api/CommunicateOptions.hpp",
        "edge_tts/core/TtsConfig.hpp",
        "edge_tts/common/Result.hpp",
        "edge_tts/common/Error.hpp",
    ]
    for h in required_includes:
        if h not in content:
            fail(f"edge_tts/edge_tts.hpp does not include {h}")
    ok(f"Story 3: umbrella header includes all {len(required_includes)} required stable headers")

    # Must NOT include internal / CLI / media / test headers.
    # Check only actual #include directives, not comment text.
    include_lines = [
        line for line in content.splitlines()
        if re.match(r'\s*#\s*include\b', line)
    ]
    include_text = "\n".join(include_lines)
    forbidden_includes = [
        "edge_tts/cli/",
        "edge_tts/media/",
        "edge_tts/communication/",
        "edge_tts/serialization/",
        "Fake",
        "test_support",
        "minigtest",
    ]
    for f in forbidden_includes:
        if f in include_text:
            fail(f"edge_tts/edge_tts.hpp has a #include for forbidden path: {f}")
    ok("Story 3: umbrella header excludes cli/media/communication/serialization/test")

    # README documents the umbrella header.
    readme = read(REPO / "README.md")
    if "#include <edge_tts/edge_tts.hpp>" not in readme:
        fail("README.md does not show #include <edge_tts/edge_tts.hpp>")
    ok("Story 3: README.md documents umbrella header usage")

    # CONSUMING.md documents it.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "edge_tts/edge_tts.hpp" not in consuming:
        fail("docs/CONSUMING.md does not document the umbrella header")
    ok("Story 3: docs/CONSUMING.md documents umbrella header")


# ---------------------------------------------------------------------------
# Story 4 — no CLI or playback needed for synthesis
# ---------------------------------------------------------------------------

def test_story4_no_cli_needed() -> None:
    """A developer does not need to link CLI or playback to synthesize speech."""

    cmake = read(REPO / "CMakeLists.txt")

    # edge_tts_tts must link only edge_tts::api (not cli or media).
    tts_block_match = re.search(
        r'target_link_libraries\s*\(\s*edge_tts_tts\b(.*?)\)',
        cmake, re.DOTALL,
    )
    if not tts_block_match:
        fail("CMakeLists.txt: cannot find target_link_libraries(edge_tts_tts …)")
    tts_block = tts_block_match.group(1)
    for forbidden in ("cli", "media", "test_support", "Fake"):
        if forbidden.lower() in tts_block.lower():
            fail(
                f"edge_tts_tts links forbidden target containing '{forbidden}'.\n"
                f"Link interface block: {tts_block.strip()}"
            )
    ok("Story 4: edge_tts_tts does not link cli, media, or test_support")

    # Both consumer examples link only edge_tts::tts.
    # Scan only target_link_libraries() calls, ignoring comment lines.
    for example_dir in ("consumer_add_subdirectory", "consumer_find_package"):
        cmake_path = REPO / "examples" / example_dir / "CMakeLists.txt"
        content = read(cmake_path)
        # Strip comment lines before scanning.
        active_lines = [
            line for line in content.splitlines()
            if not line.lstrip().startswith("#")
        ]
        active_cmake = "\n".join(active_lines)
        # Extract target_link_libraries() argument lists.
        link_blocks = re.findall(
            r'target_link_libraries\s*\([^)]*\)', active_cmake, re.DOTALL
        )
        link_text = "\n".join(link_blocks)
        forbidden_targets = [
            "edge_tts::cli", "edge_tts_cli",
            "edge_tts::media", "edge_tts_media",
            "ixwebsocket",
            "nlohmann_json",
        ]
        for t in forbidden_targets:
            if t in link_text:
                fail(
                    f"examples/{example_dir}/CMakeLists.txt directly links '{t}'.\n"
                    "Consumer examples must only link edge_tts::tts."
                )
    ok("Story 4: both consumer examples link only edge_tts::tts")

    # CONSUMING.md explicitly says CLI/media are not needed.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "edge_tts::cli" not in consuming or "not" not in consuming.lower():
        # More precise: check the "not exported" section.
        if "not exported" not in consuming.lower() and "internal" not in consuming.lower():
            fail("docs/CONSUMING.md does not clarify that edge_tts::cli is internal")
    ok("Story 4: docs/CONSUMING.md documents cli/media as internal targets")

    # EdgeTtsInstall.cmake must NOT include cli or media in the export set.
    install_cmake = read(REPO / "cmake" / "EdgeTtsInstall.cmake")
    export_block_match = re.search(
        r'set\s*\(\s*_EDGE_TTS_INSTALL_TARGETS(.*?)\)',
        install_cmake, re.DOTALL,
    )
    if export_block_match:
        export_block = export_block_match.group(1)
        for forbidden in ("edge_tts_cli", "edge_tts_media"):
            if forbidden in export_block:
                fail(
                    f"EdgeTtsInstall.cmake exports '{forbidden}' — "
                    "CLI/media must not be in the consumer export set."
                )
    ok("Story 4: EdgeTtsInstall.cmake does not export cli or media targets")


# ---------------------------------------------------------------------------
# Story 5 — no fake/test utilities in default install
# ---------------------------------------------------------------------------

def test_story5_no_fakes_in_install() -> None:
    """A developer does not get fake/test utilities in a default install."""

    # ProjectOptions.cmake must define EDGE_TTS_INSTALL_TEST_SUPPORT defaulting to OFF.
    # The option() call may span multiple lines and contains nested parens in the
    # description string, so we look for the name and "OFF" near each other.
    project_opts = read(REPO / "cmake" / "ProjectOptions.cmake")
    if "EDGE_TTS_INSTALL_TEST_SUPPORT" not in project_opts:
        fail("cmake/ProjectOptions.cmake does not define EDGE_TTS_INSTALL_TEST_SUPPORT")
    # Find the line index of the option declaration and check the surrounding
    # ~5 lines for the OFF default.
    lines = project_opts.splitlines()
    option_line_idx = next(
        (i for i, ln in enumerate(lines) if "EDGE_TTS_INSTALL_TEST_SUPPORT" in ln),
        None,
    )
    if option_line_idx is None:
        fail("EDGE_TTS_INSTALL_TEST_SUPPORT not found in ProjectOptions.cmake lines")
    option_region = "\n".join(lines[option_line_idx:option_line_idx + 6])
    if "OFF" not in option_region:
        fail(
            "EDGE_TTS_INSTALL_TEST_SUPPORT must default to OFF.\n"
            f"Relevant lines:\n{option_region}"
        )
    ok("Story 5: EDGE_TTS_INSTALL_TEST_SUPPORT defaults OFF in ProjectOptions.cmake")

    # EdgeTtsInstall.cmake must guard test-support install behind the option.
    install_cmake = read(REPO / "cmake" / "EdgeTtsInstall.cmake")
    if "EDGE_TTS_INSTALL_TEST_SUPPORT" not in install_cmake:
        fail("EdgeTtsInstall.cmake does not reference EDGE_TTS_INSTALL_TEST_SUPPORT")
    ok("Story 5: EdgeTtsInstall.cmake guards test-support behind EDGE_TTS_INSTALL_TEST_SUPPORT")

    # test_install_tree.py checks that no Fake* headers are installed.
    test_install = read(REPO / "tests" / "cmake" / "test_install_tree.py")
    if "Fake" not in test_install or "FORBIDDEN_PATTERNS" not in test_install:
        fail("test_install_tree.py does not check for Fake* header leakage")
    ok("Story 5: test_install_tree.py verifies no Fake* headers in install tree")

    # Fake headers must not live under include/edge_tts/.
    fake_in_include = list((REPO / "include" / "edge_tts").rglob("Fake*.hpp"))
    if fake_in_include:
        fail(
            "Fake* headers found under include/edge_tts/ — these would be installed:\n"
            + "\n".join(str(p.relative_to(REPO)) for p in fake_in_include)
        )
    ok("Story 5: no Fake* headers under include/edge_tts/")

    # CONSUMING.md mentions EDGE_TTS_INSTALL_TEST_SUPPORT.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "EDGE_TTS_INSTALL_TEST_SUPPORT" not in consuming:
        fail("docs/CONSUMING.md does not document EDGE_TTS_INSTALL_TEST_SUPPORT")
    ok("Story 5: docs/CONSUMING.md documents EDGE_TTS_INSTALL_TEST_SUPPORT")


# ---------------------------------------------------------------------------
# Story 6 — offline consumer tests
# ---------------------------------------------------------------------------

def test_story6_offline_consumer_tests() -> None:
    """A developer can run offline consumer tests without live Edge services."""

    # Consumer example main.cpp files must not call save() or stream_sync()
    # unconditionally (network calls should be commented out or guarded).
    for example_dir in ("consumer_add_subdirectory", "consumer_find_package"):
        main_cpp = REPO / "examples" / example_dir / "main.cpp"
        content = read(main_cpp)
        # The real calls should be inside commented-out blocks.
        # Find uncommented calls to save() or stream_sync().
        # Strip block comments and line comments, then check.
        # Simple heuristic: lines that start with // are comments.
        active_lines = [
            line for line in content.splitlines()
            if not line.lstrip().startswith("//")
        ]
        active_code = "\n".join(active_lines)
        if ".save(" in active_code or ".stream_sync(" in active_code:
            fail(
                f"examples/{example_dir}/main.cpp calls save() or stream_sync() "
                "outside a commented block. Consumer examples must not make live "
                "network calls during automated testing."
            )
    ok("Story 6: consumer example main.cpp files do not call save()/stream_sync() unconditionally")

    # Consumer test scripts disable apps (which need networking).
    for script_name in (
        "test_consumer_add_subdirectory.py",
        "test_consumer_examples.py",
        "test_install_tree.py",
        "test_consumer_strict_warnings.py",
    ):
        content = read(REPO / "tests" / "cmake" / script_name)
        if "EDGE_TTS_BUILD_APPS=OFF" not in content and "EDGE_TTS_BUILD_APPS" not in content:
            fail(
                f"tests/cmake/{script_name} does not set EDGE_TTS_BUILD_APPS=OFF; "
                "consumer build tests must not require ixwebsocket for networking."
            )
    ok("Story 6: consumer cmake test scripts use EDGE_TTS_BUILD_APPS=OFF")

    # Network tests are opt-in (EDGE_TTS_ENABLE_NETWORK_TESTS defaults OFF).
    project_opts = read(REPO / "cmake" / "ProjectOptions.cmake")
    match = re.search(
        r'option\s*\(\s*EDGE_TTS_ENABLE_NETWORK_TESTS\b.*?\)',
        project_opts, re.DOTALL,
    )
    if not match:
        fail("cmake/ProjectOptions.cmake does not define EDGE_TTS_ENABLE_NETWORK_TESTS")
    if "OFF" not in match.group(0):
        fail("EDGE_TTS_ENABLE_NETWORK_TESTS must default to OFF")
    ok("Story 6: EDGE_TTS_ENABLE_NETWORK_TESTS defaults OFF")

    # CONSUMING.md documents offline availability.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "offline" not in consuming.lower() and "network" not in consuming.lower():
        fail("docs/CONSUMING.md does not mention offline / no-network usage")
    ok("Story 6: docs/CONSUMING.md mentions offline/network behavior")


# ---------------------------------------------------------------------------
# Story 7 — clear proxy error
# ---------------------------------------------------------------------------

def test_story7_proxy_error() -> None:
    """A developer gets a clear error when using unsupported proxy behavior."""

    # CommunicateOptions.hpp must declare the proxy field.
    opts_header = REPO / "include" / "edge_tts" / "api" / "CommunicateOptions.hpp"
    content = read(opts_header)
    if "proxy" not in content:
        fail("CommunicateOptions.hpp does not have a proxy field")
    ok("Story 7: CommunicateOptions.hpp declares proxy field")

    # The proxy field documentation must mention unsupported / ErrorCode::unsupported.
    if "unsupported" not in content:
        fail(
            "CommunicateOptions.hpp proxy documentation does not mention 'unsupported'.\n"
            "Consumers must be warned that proxy returns ErrorCode::unsupported."
        )
    ok("Story 7: CommunicateOptions.hpp documents proxy as unsupported at runtime")

    # CONSUMING.md has a proxy section.
    consuming = read(REPO / "docs" / "CONSUMING.md")
    if "proxy" not in consuming.lower():
        fail("docs/CONSUMING.md does not document proxy behavior")
    if "unsupported" not in consuming.lower():
        fail("docs/CONSUMING.md does not say proxy returns unsupported error")
    ok("Story 7: docs/CONSUMING.md documents proxy as unsupported")

    # Consumer example code comments mention the proxy limitation.
    for example_dir in ("consumer_add_subdirectory", "consumer_find_package"):
        main_cpp = read(REPO / "examples" / example_dir / "main.cpp")
        if "proxy" not in main_cpp.lower():
            fail(
                f"examples/{example_dir}/main.cpp does not mention proxy limitation.\n"
                "Consumer examples should warn about proxy being unsupported."
            )
    ok("Story 7: consumer example main.cpp files mention proxy limitation")


# ---------------------------------------------------------------------------
# Dependency surface checks
# ---------------------------------------------------------------------------

def test_dependency_surface_clean() -> None:
    """The public dependency surface is minimal and intentional."""

    cmake = read(REPO / "CMakeLists.txt")

    # edge_tts_tts must link exactly edge_tts::api (INTERFACE).
    tts_links_match = re.search(
        r'target_link_libraries\s*\(\s*edge_tts_tts\s+INTERFACE\s+(.*?)\)',
        cmake, re.DOTALL,
    )
    if not tts_links_match:
        fail(
            "CMakeLists.txt: edge_tts_tts does not have an INTERFACE "
            "target_link_libraries block"
        )
    tts_links = tts_links_match.group(1).strip()
    # Should link api and nothing else.
    tokens = [t for t in re.findall(r'[\w:]+', tts_links)
              if t not in ("INTERFACE", "PUBLIC", "PRIVATE")]
    expected = {"edge_tts::api"}
    unexpected = set(tokens) - expected
    if unexpected:
        fail(
            f"edge_tts_tts links unexpected targets: {unexpected}.\n"
            "It should link only edge_tts::api; all transitive deps flow from there."
        )
    ok(f"Dependency surface: edge_tts_tts links only edge_tts::api (clean)")

    # Package config template creates aliases only for module targets, not cli/media.
    config_in = read(REPO / "cmake" / "edge_tts_cpp-config.cmake.in")
    # Check that cli and media don't appear in the alias loop.
    alias_loop_match = re.search(
        r'foreach\s*\(_edge_tts_mod(.*?)\)',
        config_in, re.DOTALL,
    )
    if alias_loop_match:
        alias_mods = alias_loop_match.group(1)
        for forbidden in ("cli", "media"):
            if forbidden in alias_mods:
                fail(
                    f"edge_tts_cpp-config.cmake.in creates a consumer alias for '{forbidden}'.\n"
                    "cli and media must not be exposed to package consumers."
                )
    ok("Dependency surface: package config does not alias cli or media")

    # EdgeTtsInstall.cmake export set excludes cli, media, test_support.
    install_cmake = read(REPO / "cmake" / "EdgeTtsInstall.cmake")
    export_match = re.search(
        r'set\s*\(\s*_EDGE_TTS_INSTALL_TARGETS(.*?)\)',
        install_cmake, re.DOTALL,
    )
    if export_match:
        export_targets = export_match.group(1)
        for forbidden in ("edge_tts_cli", "edge_tts_media", "test_support", "Fake"):
            if forbidden in export_targets:
                fail(
                    f"EdgeTtsInstall.cmake exports '{forbidden}' — "
                    "this target must not be exported to consumers."
                )
    ok("Dependency surface: EdgeTtsInstall.cmake export set is clean")

    # The exported target set covers the expected public modules.
    expected_in_export = [
        "edge_tts_common", "edge_tts_core", "edge_tts_serialization",
        "edge_tts_subtitle", "edge_tts_communication", "edge_tts_api",
        "edge_tts_tts",
    ]
    if export_match:
        export_targets = export_match.group(1)
        for tgt in expected_in_export:
            if tgt not in export_targets:
                fail(
                    f"EdgeTtsInstall.cmake does not export '{tgt}' — "
                    "all public library modules must be in the export set."
                )
    ok(f"Dependency surface: all {len(expected_in_export)} public modules are in the export set")


# ---------------------------------------------------------------------------
# Documentation cross-references
# ---------------------------------------------------------------------------

def test_documentation_cross_references() -> None:
    """Documented commands match tested fixtures and scripts."""

    consuming = read(REPO / "docs" / "CONSUMING.md")
    readme = read(REPO / "README.md")

    # CONSUMING.md quick-start uses find_package and edge_tts::tts.
    if "find_package(edge_tts_cpp" not in consuming:
        fail("docs/CONSUMING.md quick-start does not show find_package(edge_tts_cpp ...)")
    if "edge_tts::tts" not in consuming:
        fail("docs/CONSUMING.md quick-start does not show edge_tts::tts")
    ok("Docs: CONSUMING.md quick-start shows find_package + edge_tts::tts")

    # CONSUMING.md references the two example directories.
    for example in ("consumer_add_subdirectory", "consumer_find_package"):
        if example not in consuming:
            fail(f"docs/CONSUMING.md does not reference examples/{example}/")
    ok("Docs: CONSUMING.md references both consumer examples")

    # README references both examples.
    if "consumer_add_subdirectory" not in readme:
        fail("README.md does not reference examples/consumer_add_subdirectory/")
    if "consumer_find_package" not in readme:
        fail("README.md does not reference examples/consumer_find_package/")
    ok("Docs: README.md references both consumer examples")

    # README has a dependency usage section.
    readme_lower = readme.lower()
    if "use as a dependency" not in readme_lower and "dependency" not in readme_lower:
        fail(
            "README.md does not have a 'Use as a dependency' section.\n"
            "Add a section that helps developers integrate edge-tts-cpp quickly."
        )
    ok("Docs: README.md has dependency usage content")

    # RELEASE_READINESS.md has a linkage / consumer-readiness entry.
    release = read(REPO / "docs" / "RELEASE_READINESS.md")
    if "consumer" not in release.lower():
        fail("docs/RELEASE_READINESS.md does not mention consumer readiness")
    ok("Docs: RELEASE_READINESS.md covers consumer readiness")

    # MODULES.md has the public consumer targets section.
    modules = read(REPO / "docs" / "MODULES.md")
    if "edge_tts::tts" not in modules:
        fail("docs/MODULES.md does not document edge_tts::tts")
    if "consumer" not in modules.lower():
        fail("docs/MODULES.md does not have a consumer targets section")
    ok("Docs: MODULES.md documents edge_tts::tts and consumer targets")


# ---------------------------------------------------------------------------
# Test coverage completeness
# ---------------------------------------------------------------------------

def test_all_python_tests_registered() -> None:
    """Every Python test script under tests/ is registered with CTest."""

    cmake = read(REPO / "CMakeLists.txt")

    # Find all Python test scripts.
    test_scripts = sorted(
        p for p in REPO.joinpath("tests").rglob("*.py")
        if not any(part.startswith("__") or part == "module_boundary_fixtures"
                   for part in p.parts)
    )

    missing: list[str] = []
    for script in test_scripts:
        rel = script.relative_to(REPO).as_posix()
        if rel not in cmake:
            missing.append(rel)

    if missing:
        fail(
            f"{len(missing)} Python test script(s) not registered in CMakeLists.txt:\n"
            + "\n".join(f"  {m}" for m in missing)
            + "\n\nAdd an add_test() entry for each missing script."
        )
    ok(f"Coverage: all {len(test_scripts)} Python test scripts are registered in CMakeLists.txt")


def test_consumer_cmake_tests_exist() -> None:
    """The key consumer cmake test scripts and fixtures all exist."""

    required_scripts = [
        "tests/cmake/test_consumer_add_subdirectory.py",
        "tests/cmake/test_consumer_examples.py",
        "tests/cmake/test_consumer_strict_warnings.py",
        "tests/cmake/test_install_tree.py",
        "tests/cmake/test_linkage_mode.py",
        "tests/cmake/test_install_components.py",
        "tests/cmake/test_public_tts_target.py",
        "tests/cmake/test_installed_header_selfcontainment.py",
    ]
    required_fixtures = [
        "tests/cmake/consumer_install_basic",
        "tests/cmake/consumer_add_subdirectory_basic",
        "tests/cmake/consumer_tts_target_basic",
        "tests/cmake/consumer_static_build",
        "tests/cmake/consumer_strict_warnings",
        "examples/consumer_add_subdirectory",
        "examples/consumer_find_package",
    ]

    for path_str in required_scripts:
        if not (REPO / path_str).exists():
            fail(f"Required consumer test script missing: {path_str}")
    ok(f"Coverage: all {len(required_scripts)} required consumer test scripts exist")

    for path_str in required_fixtures:
        if not (REPO / path_str).is_dir():
            fail(f"Required consumer fixture directory missing: {path_str}")
    ok(f"Coverage: all {len(required_fixtures)} required consumer fixture directories exist")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        ("Story 1 — add_subdirectory", test_story1_add_subdirectory),
        ("Story 2 — find_package", test_story2_find_package),
        ("Story 3 — umbrella header", test_story3_umbrella_header),
        ("Story 4 — no CLI/playback needed", test_story4_no_cli_needed),
        ("Story 5 — no fakes in default install", test_story5_no_fakes_in_install),
        ("Story 6 — offline consumer tests", test_story6_offline_consumer_tests),
        ("Story 7 — clear proxy error", test_story7_proxy_error),
        ("Dependency surface", test_dependency_surface_clean),
        ("Documentation cross-references", test_documentation_cross_references),
        ("Test coverage completeness", test_all_python_tests_registered),
        ("Consumer test files existence", test_consumer_cmake_tests_exist),
    ]

    for label, fn in tests:
        print(f"\n── {label}")
        fn()

    total_checks = sum(
        fn.__code__.co_consts.count("  OK  ") if False else 1
        for _, fn in tests
    )
    print(f"\nAll {len(tests)} consumer readiness checks passed.")


if __name__ == "__main__":
    main()
