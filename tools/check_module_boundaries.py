#!/usr/bin/env python3
"""
check_module_boundaries.py — Static include-boundary checker for edge-tts-cpp.

Scans C/C++ source and header files for #include directives that violate the
module dependency rules documented in docs/DEPENDENCY_RULES.md.

Rules checked
─────────────
1. Module-layer rule: a module may only #include headers from modules it is
   allowed to depend on (see ALLOWED_DEPS below).
2. Private-header rule: files under apps/ must not include headers whose path
   resolves through src/ (i.e. private implementation headers).

Usage
─────
    python3 tools/check_module_boundaries.py [--root <repo-root>] [--verbose]

Exit codes
──────────
    0  no violations found
    1  one or more violations found
    2  usage/configuration error
"""

import argparse
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Dependency rules (filesystem module names, not CMake target names)
# ---------------------------------------------------------------------------
# Each key is a module name as it appears on the filesystem
# (include/edge_tts/<module>/ and src/<module>/).
# The value is the set of module names that module is ALLOWED to include.
# Including a module not in this set is a violation.
ALLOWED_DEPS: dict[str, set[str]] = {
    "common":        set(),
    "core":          {"common"},
    "serialization": {"common", "core"},
    "subtitles":     {"common", "core"},
    "media":         {"common"},
    # communication is pure transport: WebSocket/HTTP framing, protocol parsing,
    # session lifecycle.  It must not include subtitle or media — those are owned
    # by api above it.
    "communication": {"common", "core", "serialization"},
    # api is the public facade above communication; it may include everything below.
    "api":           {"common", "core", "serialization", "subtitles", "media",
                      "communication"},
    # cli may depend on api (and transitively everything api exposes), plus media
    # directly (public header PlaybackCommandDispatcher.hpp includes AudioConverter)
    # and subtitles directly (EdgeTtsCommandDispatcher.cpp uses SubMaker).
    # cli must not reach past api into communication or serialization internals.
    "cli":           {"common", "core", "subtitles", "media", "api"},
    # apps/ executables sit above the cli layer and may include any module.
    "apps":          {"common", "core", "serialization", "subtitles", "media",
                      "communication", "api", "cli"},
    # umbrella: top-level headers directly under include/edge_tts/ (e.g. edge_tts.hpp).
    # They aggregate the stable consumer-facing API: api, core, common.
    # They must not include cli, media, communication, or serialization.
    "umbrella":      {"common", "core", "api"},
}

# Known module names — used to validate that a scanned file belongs to a
# recognised module and that unknown names in includes are not false-positives.
# KNOWN_MODULES excludes "apps" and "umbrella" — they are file-location
# sentinels, not includable module names (no include/edge_tts/apps/ or
# include/edge_tts/umbrella/ directory exists).
KNOWN_MODULES: frozenset[str] = frozenset(ALLOWED_DEPS) - {"apps", "umbrella"}

# Extensions to scan.
SCAN_EXTENSIONS: frozenset[str] = frozenset({".cpp", ".hpp", ".h", ".cc", ".cxx"})

# Regex: captures the path inside an edge_tts #include, e.g.
#   #include "edge_tts/core/TtsConfig.hpp"   → "core/TtsConfig.hpp"
#   #include <edge_tts/common/Errors.hpp>    → "common/Errors.hpp"
_EDGE_INCLUDE_RE = re.compile(
    r'#\s*include\s+[<"]edge_tts/([^>"]+)[>"]'
)

# Regex: catches includes that reach into src/ through relative paths.
# Examples: #include "../../src/core/Foo.hpp"  or  #include "../src/bar.hpp"
_PRIVATE_INCLUDE_RE = re.compile(
    r'#\s*include\s+"[^"]*\bsrc\b[/\\][^"]+'
)


# ---------------------------------------------------------------------------
# Core logic (importable for unit tests)
# ---------------------------------------------------------------------------

def module_of_file(path: Path, root: Path) -> str | None:
    """
    Return the module name for *path* relative to *root*, or None if the file
    does not belong to a scanned module.

    Recognised file locations:
      include/edge_tts/<module>/...  → <module>
      src/<module>/...               → <module>
      apps/...                       → "apps"
    """
    try:
        rel = path.relative_to(root)
    except ValueError:
        return None

    parts = rel.parts
    if len(parts) >= 3 and parts[0] == "include" and parts[1] == "edge_tts":
        if len(parts) == 3:
            # File sits directly at include/edge_tts/<file>.hpp — umbrella header.
            return "umbrella"
        return parts[2]
    if len(parts) >= 2 and parts[0] == "src":
        return parts[1]
    if len(parts) >= 2 and parts[0] == "apps":
        return "apps"
    return None


def violations_in_file(path: Path, root: Path) -> list[str]:
    """
    Return a list of human-readable violation strings found in *path*.
    Returns an empty list if the file is clean or not in a scanned location.
    """
    module = module_of_file(path, root)
    if module is None:
        return []

    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        return [f"{path}: cannot read file: {exc}"]

    found: list[str] = []

    for lineno, line in enumerate(lines, start=1):
        # --- Rule 1: module dependency layer ---
        m = _EDGE_INCLUDE_RE.search(line)
        if m:
            inc_path = m.group(1)           # e.g. "core/TtsConfig.hpp"
            inc_module = inc_path.split("/")[0]  # e.g. "core"

            if inc_module not in KNOWN_MODULES:
                # Unknown module name in include — not our business to reject.
                continue

            # A file is always allowed to include its own module.
            if inc_module == module:
                continue

            allowed = ALLOWED_DEPS.get(module, set())
            if inc_module not in allowed:
                found.append(
                    f"{path}:{lineno}: [{module}] forbidden include of "
                    f"[{inc_module}]: {line.strip()}"
                )

        # --- Rule 2: private-header rule (apps only) ---
        if module == "apps" and _PRIVATE_INCLUDE_RE.search(line):
            found.append(
                f"{path}:{lineno}: [apps] private header include "
                f"(path through src/): {line.strip()}"
            )

    return found


def scan_tree(root: Path, verbose: bool = False) -> list[str]:
    """
    Walk *root* recursively and return all violations found.
    Skips hidden directories and the reference/ subtree.
    """
    all_violations: list[str] = []

    # Top-level directories (relative to root) to skip entirely.
    SKIP_TOP = {"tests", "tools", ".git", ".claude", "cmake-build-debug",
                "build", "reference", "submodules", ".idea"}

    for path in sorted(root.rglob("*")):
        # Skip hidden dirs and known non-project dirs only at the root level.
        try:
            top = path.relative_to(root).parts[0]
        except (ValueError, IndexError):
            continue
        if top in SKIP_TOP:
            continue
        if not path.is_file():
            continue
        if path.suffix not in SCAN_EXTENSIONS:
            continue

        if verbose:
            print(f"  scanning {path.relative_to(root)}", file=sys.stderr)

        violations = violations_in_file(path, root)
        all_violations.extend(violations)

    return all_violations


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check edge-tts-cpp module include boundaries."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Repository root directory (default: parent of tools/)",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Print each scanned file to stderr",
    )
    args = parser.parse_args()

    root: Path = args.root.resolve()
    if not root.is_dir():
        print(f"ERROR: root directory not found: {root}", file=sys.stderr)
        return 2

    violations = scan_tree(root, verbose=args.verbose)

    if violations:
        for v in violations:
            print(v)
        print(f"\n{len(violations)} boundary violation(s) found.", file=sys.stderr)
        return 1

    print(f"OK: no boundary violations found (scanned from {root}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
