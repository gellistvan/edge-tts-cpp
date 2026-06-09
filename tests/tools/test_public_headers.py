#!/usr/bin/env python3
"""
Static analysis tests for all public headers under include/edge_tts/.

Verifies — without compilation — that the public header tree is hygienic:

  1. Every header has a #pragma once guard.
  2. No header includes from src/, tests/, or absolute filesystem paths.
  3. No header includes Fake* test-double headers.
  4. Stable headers (api/, core/, common/, subtitles/, edge_tts.hpp) only
     introduce declarations inside the edge_tts:: namespace.
  5. The public header directory contains no files other than .hpp files
     (no generated headers, no stray .cpp or .h.in files).

"Stable" means "part of the edge_tts::tts consumer-facing API".
communication/, serialization/, media/, and cli/ headers are installed but are
not part of the stable API — they are only validated by checks 1–3 + 5.

Exit code 0 on success, non-zero on failure.
"""

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
MODULES_DIR = REPO_ROOT / "modules"
UMBRELLA_DIR = REPO_ROOT / "include" / "edge_tts"

# Stable module directory names (consumer-facing API, subject to stricter checks).
STABLE_MODULES = {"api", "core", "common", "subtitles"}


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"  OK  {msg}")


def all_headers() -> list[pathlib.Path]:
    """Return all public headers: module includes + umbrella."""
    headers: list[pathlib.Path] = []
    if MODULES_DIR.exists():
        for mod_dir in sorted(MODULES_DIR.iterdir()):
            include_dir = mod_dir / "include"
            if include_dir.exists():
                headers.extend(include_dir.rglob("*.hpp"))
    if UMBRELLA_DIR.exists():
        headers.extend(UMBRELLA_DIR.rglob("*.hpp"))
    return sorted(headers)


def is_stable(header: pathlib.Path) -> bool:
    """Return True if the header belongs to a stable consumer-facing module."""
    # Umbrella header
    if header.parent == UMBRELLA_DIR:
        return True
    # Check if it lives under a stable module's include subdir
    try:
        rel = header.relative_to(MODULES_DIR)
    except ValueError:
        return False
    # rel.parts[0] = module dir name, rel.parts[1] = "include", rel.parts[2] = subdir name
    if len(rel.parts) >= 3 and rel.parts[1] == "include":
        return rel.parts[2] in STABLE_MODULES
    return False


def strip_line_comments(line: str) -> str:
    """Remove everything after // on a line."""
    idx = line.find("//")
    return line[:idx] if idx != -1 else line


def strip_block_comments(source: str) -> str:
    """Remove /* ... */ comments (including multi-line)."""
    return re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)


# ---------------------------------------------------------------------------
# Check 1: #pragma once
# ---------------------------------------------------------------------------

def test_pragma_once() -> None:
    headers = all_headers()
    missing = []
    for h in headers:
        content = h.read_text(encoding="utf-8")
        if "#pragma once" not in content:
            missing.append(str(h.relative_to(REPO_ROOT)))
    if missing:
        fail(
            f"{len(missing)} header(s) missing #pragma once:\n  "
            + "\n  ".join(missing)
        )
    ok(f"All {len(headers)} headers have #pragma once")


# ---------------------------------------------------------------------------
# Check 2: No includes from src/, tests/, or absolute paths
# ---------------------------------------------------------------------------

_PRIVATE_INCLUDE_PATTERNS = [
    # Pattern                     Reason
    (re.compile(r'#\s*include\s+[<"]\.\./'), "relative ../ path — escaped from include tree"),
    (re.compile(r'#\s*include\s+"[/~]'),     "absolute path in quoted include"),
    (re.compile(r'#\s*include\s+<[/~]'),     "absolute path in angle-bracket include"),
    (re.compile(r'#\s*include\s+[<"]src/'),  "src/ path — implementation-only; forbidden in headers"),
    (re.compile(r'#\s*include\s+[<"]tests/'), "tests/ path — test infrastructure; forbidden in headers"),
    (re.compile(r'#\s*include\s+[<"]apps/'),  "apps/ path — CLI app; forbidden in public headers"),
]


def test_no_private_includes() -> None:
    violations: list[str] = []
    for h in all_headers():
        content = strip_block_comments(h.read_text(encoding="utf-8"))
        rel = str(h.relative_to(REPO_ROOT))
        for lineno, raw_line in enumerate(content.splitlines(), 1):
            line = strip_line_comments(raw_line)
            for pattern, reason in _PRIVATE_INCLUDE_PATTERNS:
                if pattern.search(line):
                    violations.append(
                        f"  {rel}:{lineno}: {raw_line.strip()}\n    → {reason}"
                    )
    if violations:
        fail(
            f"{len(violations)} private-include violation(s) found:\n"
            + "\n".join(violations)
        )
    ok(f"No private-include violations in {len(all_headers())} headers")


# ---------------------------------------------------------------------------
# Check 3: No Fake* test-double includes
# ---------------------------------------------------------------------------

_FAKE_INCLUDE_RE = re.compile(
    r'#\s*include\s+[<"][^>"]*Fake[A-Z][^>"]*\.hpp[>"]'
)


def test_no_fake_includes() -> None:
    violations: list[str] = []
    for h in all_headers():
        content = strip_block_comments(h.read_text(encoding="utf-8"))
        rel = str(h.relative_to(REPO_ROOT))
        for lineno, raw_line in enumerate(content.splitlines(), 1):
            line = strip_line_comments(raw_line)
            if _FAKE_INCLUDE_RE.search(line):
                violations.append(f"  {rel}:{lineno}: {raw_line.strip()}")
    if violations:
        fail(
            "Fake* test-double headers found in public headers "
            "(must never appear in the install tree):\n" + "\n".join(violations)
        )
    ok("No Fake* test-double includes in any public header")


# ---------------------------------------------------------------------------
# Check 4: Stable headers only introduce declarations in edge_tts::
# ---------------------------------------------------------------------------

# Match a namespace opening that is NOT nested inside another namespace
# already.  We look for lines like:
#   namespace foo {
#   namespace foo::bar {
#   namespace foo {  // comment
# We deliberately ignore `namespace {` (anonymous) — those are bad practice in
# headers but are not a "namespace leakage" concern for consumers.
_NAMESPACE_DECL_RE = re.compile(
    r"^\s*namespace\s+([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*)\s*[{;]?"
)


def test_stable_headers_namespace() -> None:
    """
    Stable headers must not introduce top-level namespace declarations whose
    first component is not 'edge_tts'.

    Method: scan each stable header (comments stripped) for namespace
    declarations and verify every identifier starts with edge_tts.
    """
    stable_headers = [h for h in all_headers() if is_stable(h)]
    violations: list[str] = []

    for h in stable_headers:
        raw = h.read_text(encoding="utf-8")
        source = strip_block_comments(raw)
        rel = str(h.relative_to(REPO_ROOT))

        for lineno, line in enumerate(source.splitlines(), 1):
            clean = strip_line_comments(line)
            m = _NAMESPACE_DECL_RE.match(clean)
            if not m:
                continue
            first_component = m.group(1).split("::")[0]
            if first_component != "edge_tts":
                # Get the original line for the error message.
                orig_line = raw.splitlines()[lineno - 1].strip()
                violations.append(
                    f"  {rel}:{lineno}: {orig_line}\n"
                    f"    → namespace '{m.group(1)}' is not inside edge_tts::"
                )

    if violations:
        fail(
            "Stable public headers introduce non-edge_tts namespace declarations:\n"
            + "\n".join(violations)
        )
    ok(
        f"All {len(stable_headers)} stable headers only expose "
        "edge_tts:: namespace declarations"
    )


# ---------------------------------------------------------------------------
# Check 5: No non-.hpp files under include/edge_tts/
# ---------------------------------------------------------------------------

def test_only_hpp_in_include() -> None:
    non_hpp: list[str] = []
    # Check umbrella include dir
    if UMBRELLA_DIR.exists():
        for p in UMBRELLA_DIR.rglob("*"):
            if p.is_file() and p.suffix != ".hpp":
                non_hpp.append(str(p.relative_to(REPO_ROOT)))
    # Check each module's include dir
    if MODULES_DIR.exists():
        for mod_dir in sorted(MODULES_DIR.iterdir()):
            include_dir = mod_dir / "include"
            if include_dir.exists():
                for p in include_dir.rglob("*"):
                    if p.is_file() and p.suffix != ".hpp":
                        non_hpp.append(str(p.relative_to(REPO_ROOT)))
    if non_hpp:
        fail(
            "Non-.hpp files found in module include/ trees "
            "(generated headers, .h.in, etc. must not be in the public install tree):\n  "
            + "\n  ".join(non_hpp)
        )
    ok("All module include/ trees contain only .hpp files")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    tests = [
        test_pragma_once,
        test_no_private_includes,
        test_no_fake_includes,
        test_stable_headers_namespace,
        test_only_hpp_in_include,
    ]
    for t in tests:
        t()
    print(f"\nAll {len(tests)} public header hygiene checks passed.")


if __name__ == "__main__":
    main()
