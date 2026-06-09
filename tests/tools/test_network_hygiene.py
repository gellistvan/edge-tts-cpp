#!/usr/bin/env python3
"""
Network hygiene tests for edge-tts-cpp.

Verifies that the default C++ test suite cannot accidentally contact external
network services:

  1. Normal test files must not call synthesize()/save() on SpeechSynthesizer
     objects that were constructed WITHOUT a synthesizer-injection argument.
     The production 2-arg and 3-arg (text+config+SynthesisOptions) constructors
     wire a real WebSocketClient that will attempt TLS connections on
     synthesize()/save().  Detection uses argument-level analysis:
       - 2-arg (text, config) → always production
       - 3-arg (text, config, SynthesisOptions/opts) → production options form
       - 3-arg (text, config, <synthesizer>) → injection, safe
       - 4-arg (text, config, opts, <synthesizer>) → injection, safe

  2. All files that use EDGE_TTS_RUN_NETWORK_TESTS / network_enabled() must
     have "Network" in their filename so the CI exclusion pattern -R network
     (or -E network) captures them.

  3. tests/CMakeLists.txt must gate all network test binaries behind
     EDGE_TTS_ENABLE_NETWORK_TESTS so they are never compiled in the default
     build.

  4. Network test functions must check network_enabled() within their first
     5 executable lines to skip cleanly when EDGE_TTS_RUN_NETWORK_TESTS is unset.

  5. EDGE_TTS_RUN_NETWORK_TESTS is documented in docs/TESTING.md.

Exit code 0 on success, non-zero on first failure.
"""

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
TESTS_DIR = REPO_ROOT / "tests"

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


def _is_network_test_file(path: pathlib.Path) -> bool:
    if not path.name.endswith(".cpp"):
        return False
    # Conventional name-based marker
    if "Network" in path.name:
        return True
    # All files living under tests/network/ are network-only regardless of name
    try:
        rel = path.relative_to(TESTS_DIR)
        if rel.parts[0] == "network":
            return True
    except ValueError:
        pass
    return False


def _is_normal_test_file(path: pathlib.Path) -> bool:
    if path.suffix != ".cpp":
        return False
    try:
        rel = path.relative_to(TESTS_DIR)
    except ValueError:
        return False
    if rel.parts[0] in ("tools", "vendor"):
        return False
    if "fixture" in str(path).lower():
        return False
    return not _is_network_test_file(path)


# ---------------------------------------------------------------------------
# Argument-level SpeechSynthesizer constructor analysis
# ---------------------------------------------------------------------------

def _split_top_level_args(args_str: str) -> list[str]:
    """Split a string by commas, ignoring commas inside (), {}, []."""
    args: list[str] = []
    current = ""
    depth = 0
    for ch in args_str:
        if ch in ("(", "{", "["):
            depth += 1
        elif ch in (")", "}", "]"):
            depth -= 1
        if ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        args.append(current.strip())
    return args


def _extract_communicate_ctor_args(block: str) -> list[str] | None:
    """
    Find the first `SpeechSynthesizer varname(...)` declaration in `block` and
    return its constructor arguments as a list.  Returns None if not found.
    """
    m = re.search(r"\bCommunicate\s+\w+\s*\(", block)
    if not m:
        return None

    # Walk forward to extract the balanced content of the constructor call.
    start = block.index("(", m.start())
    depth = 0
    inner_chars: list[str] = []
    for ch in block[start:]:
        if ch == "(":
            depth += 1
            if depth == 1:
                continue  # skip the opening paren itself
        elif ch == ")":
            depth -= 1
            if depth == 0:
                break
        if depth > 0:
            inner_chars.append(ch)

    return _split_top_level_args("".join(inner_chars))


def _third_arg_is_options(arg: str) -> bool:
    """
    True if the 3rd constructor argument looks like a SynthesisOptions
    (production form) rather than a synthesizer (injection form).
    """
    # Explicit type name
    if re.search(r"\bCommunicateOptions\b", arg):
        return True
    # Common variable names for options objects
    if re.match(r"^\s*(opts|options|opt)\s*$", arg):
        return True
    return False


def _block_is_production_communicate_with_network_call(block: str) -> bool:
    """
    True when a test block:
      (a) constructs a SpeechSynthesizer with the production (non-injection) constructor,
      (b) calls .synthesize() or .save( on it.
    """
    # (b) must call stream_sync or save
    if not re.search(r"\.(stream_sync|save)\s*\(", block):
        return False

    args = _extract_communicate_ctor_args(block)
    if args is None:
        return False

    n = len(args)
    if n < 2:
        return False  # malformed — ignore

    if n == 2:
        # (text, config) — always the production constructor.
        return True

    if n == 3:
        # (text, config, X):
        #   X = SynthesisOptions → production 3-arg form
        #   X = synthesizer        → injection 3-arg form (safe)
        return _third_arg_is_options(args[2])

    # n >= 4: always has a synthesizer as the 4th arg — injection form, safe.
    return False


def _extract_test_blocks(content: str) -> list[tuple[int, str]]:
    """
    Return (start_lineno, full_block_text) for each TEST(...){...} in content.
    Uses brace counting; sufficient for our flattened test style.
    """
    blocks: list[tuple[int, str]] = []
    lines = content.split("\n")
    i = 0
    while i < len(lines):
        if re.match(r"\s*TEST\s*\(", lines[i]):
            start = i + 1
            collected: list[str] = []
            depth = 0
            found_open = False
            j = i
            while j < len(lines):
                collected.append(lines[j])
                for ch in lines[j]:
                    if ch == "{":
                        depth += 1
                        found_open = True
                    elif ch == "}":
                        depth -= 1
                if found_open and depth == 0:
                    break
                j += 1
            blocks.append((start, "\n".join(collected)))
            i = j + 1
        else:
            i += 1
    return blocks


# ---------------------------------------------------------------------------
# 1. Normal tests must not call synthesize()/save() on production SpeechSynthesizer
# ---------------------------------------------------------------------------

def test_no_production_communicate_calls_in_normal_tests() -> None:
    violations: list[str] = []

    for cpp in sorted(TESTS_DIR.rglob("*.cpp")):
        if not _is_normal_test_file(cpp):
            continue
        content = read(cpp)
        if "SpeechSynthesizer" not in content:
            continue

        for lineno, block in _extract_test_blocks(content):
            if _block_is_production_communicate_with_network_call(block):
                rel = cpp.relative_to(REPO_ROOT)
                snippet = "\n    ".join(block.split("\n")[:5]).strip()
                violations.append(f"{rel}:{lineno}\n    {snippet}")

    if violations:
        fail(
            "Normal test files contain SpeechSynthesizer objects that call synthesize() or "
            "save() WITHOUT a synthesizer-injection argument.\n"
            "These tests will attempt real TLS connections and must be moved to a "
            "network-gated file (e.g. *NetworkTests.cpp).\n\n"
            "Violations:\n" + "\n\n".join(violations)
        )
    ok(
        "No normal test file calls synthesize()/save() on a "
        "production-constructed SpeechSynthesizer"
    )


# ---------------------------------------------------------------------------
# 2. Files using EDGE_TTS_RUN_NETWORK_TESTS must be named *Network*.cpp
# ---------------------------------------------------------------------------

def _strip_comments(content: str) -> str:
    """Remove // line comments and /* block comments */ from C++ source."""
    # Remove block comments
    content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
    # Remove line comments
    content = re.sub(r"//[^\n]*", "", content)
    return content


def test_network_gate_files_have_network_in_name() -> None:
    violations: list[str] = []

    for cpp in sorted(TESTS_DIR.rglob("*.cpp")):
        try:
            rel = cpp.relative_to(TESTS_DIR)
        except ValueError:
            continue
        if rel.parts[0] in ("tools", "vendor"):
            continue
        # Files already identified as network test files by location (tests/network/)
        # or name (*Network*.cpp) are compliant — skip them.
        if _is_network_test_file(cpp):
            continue
        # Check only code — comments may legitimately reference the env var names.
        code = _strip_comments(read(cpp))
        if (
            "EDGE_TTS_RUN_NETWORK_TESTS" in code
            or "network_enabled()" in code
        ):
            violations.append(str(cpp.relative_to(REPO_ROOT)))

    if violations:
        fail(
            "These test files use EDGE_TTS_RUN_NETWORK_TESTS / network_enabled() "
            "but are NOT named *Network*.cpp and are NOT in tests/network/.\n"
            "They will be compiled and run unconditionally, bypassing the gate.\n"
            "Rename them to *NetworkTests.cpp or move them to tests/network/.\n\n"
            "Violations:\n  " + "\n  ".join(violations)
        )
    ok("All files using EDGE_TTS_RUN_NETWORK_TESTS are named *Network*.cpp or live in tests/network/")


# ---------------------------------------------------------------------------
# 3. tests/CMakeLists.txt gates all network test sources behind the option
# ---------------------------------------------------------------------------

def _sources_inside_network_gate(cmake_content: str) -> set[str]:
    """
    Return the set of source file names that appear inside an
    if(EDGE_TTS_ENABLE_NETWORK_TESTS) block in the given CMake content.
    Uses line-by-line nesting tracking to handle nested if() blocks correctly.
    """
    inside: set[str] = []
    in_block = False
    depth = 0

    for line in cmake_content.split("\n"):
        stripped = line.strip()
        if re.match(r"if\s*\(\s*EDGE_TTS_ENABLE_NETWORK_TESTS\b", stripped):
            in_block = True
            depth = 1
        elif in_block:
            if re.match(r"if\s*\(", stripped):
                depth += 1
            elif re.match(r"endif\s*\(\)", stripped):
                depth -= 1
                if depth == 0:
                    in_block = False
            else:
                # Check if any *NetworkTests.cpp name appears on this line
                for name in re.findall(r"\w+NetworkTests\.cpp", stripped):
                    inside.append(name)
    return set(inside)


def test_cmake_gates_network_test_binaries() -> None:
    cmake = TESTS_DIR / "CMakeLists.txt"
    if not cmake.exists():
        fail("tests/CMakeLists.txt does not exist")
    content = read(cmake)

    # All *NetworkTests.cpp files referenced in the CMakeLists
    all_refs = set(re.findall(r"\w+NetworkTests\.cpp", content))
    # Those that are inside an EDGE_TTS_ENABLE_NETWORK_TESTS block
    gated = _sources_inside_network_gate(content)
    # Ungated = referenced but not inside the gate
    ungated = all_refs - gated

    if ungated:
        fail(
            "Network test sources are NOT inside if(EDGE_TTS_ENABLE_NETWORK_TESTS) "
            "in tests/CMakeLists.txt:\n  " + "\n  ".join(sorted(ungated))
        )
    ok(
        "All *NetworkTests.cpp references in tests/CMakeLists.txt are inside "
        "EDGE_TTS_ENABLE_NETWORK_TESTS guards"
    )


# ---------------------------------------------------------------------------
# 4. Each network test function checks network_enabled() near its start
# ---------------------------------------------------------------------------

def test_network_tests_check_runtime_gate() -> None:
    violations: list[str] = []

    network_files = sorted(
        set(TESTS_DIR.rglob("*NetworkTests.cpp")) |
        set(TESTS_DIR.glob("network/*.cpp"))
    )
    for cpp in network_files:
        content = read(cpp)
        for lineno, block in _extract_test_blocks(content):
            # Grab the first 6 non-empty, non-brace-only lines after the opening {
            body_lines: list[str] = []
            past_open = False
            for line in block.split("\n"):
                stripped = line.strip()
                if not past_open:
                    if "{" in stripped:
                        past_open = True
                    continue
                if stripped and stripped != "{" and stripped != "}":
                    body_lines.append(stripped)
                if len(body_lines) >= 6:
                    break

            first_few = "\n".join(body_lines)
            if "network_enabled()" not in first_few:
                rel = cpp.relative_to(REPO_ROOT)
                name_m = re.match(r"\s*TEST\s*\(([^)]+)\)", block)
                test_id = (
                    name_m.group(1).replace("\n", " ") if name_m else f"line {lineno}"
                )
                violations.append(f"{rel}:{lineno} → TEST({test_id})")

    if violations:
        fail(
            "Network test functions must check network_enabled() within their first "
            "6 executable lines so they skip cleanly when EDGE_TTS_RUN_NETWORK_TESTS "
            "is unset.\n\nMissing gate:\n  " + "\n  ".join(violations)
        )
    ok(
        "All network test functions check network_enabled() at the top of their body"
    )


# ---------------------------------------------------------------------------
# 5. docs/TESTING.md documents the network env vars
# ---------------------------------------------------------------------------

def test_testing_md_documents_network_env_var() -> None:
    testing_md = REPO_ROOT / "docs" / "TESTING.md"
    if not testing_md.exists():
        fail("docs/TESTING.md does not exist")
    content = read(testing_md)
    for token in ("EDGE_TTS_RUN_NETWORK_TESTS", "EDGE_TTS_ENABLE_NETWORK_TESTS"):
        if token not in content:
            fail(f"docs/TESTING.md does not document {token}")
    ok(
        "docs/TESTING.md documents both EDGE_TTS_RUN_NETWORK_TESTS and "
        "EDGE_TTS_ENABLE_NETWORK_TESTS"
    )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_no_production_communicate_calls_in_normal_tests,
        test_network_gate_files_have_network_in_name,
        test_cmake_gates_network_test_binaries,
        test_network_tests_check_runtime_gate,
        test_testing_md_documents_network_env_var,
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} network hygiene checks passed.")


if __name__ == "__main__":
    main()
