# Testing Guide

## Test framework

All C++ tests use `tests/vendor/minigtest/minigtest.hpp`, a self-contained
GTest-compatible single-header runner.  No external test framework installation
is required.  The supported macro surface is:

```
TEST(Suite, Name)
EXPECT_EQ / EXPECT_NE / EXPECT_TRUE / EXPECT_FALSE
ASSERT_EQ / ASSERT_NE / ASSERT_TRUE / ASSERT_FALSE
EXPECT_THROW(stmt, ExceptionType)
EXPECT_NO_THROW(stmt)
```

## Running tests

```bash
cmake -S . -B build -DEDGE_TTS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Run a specific test by name pattern
ctest --test-dir build -R edge_tts_core_tests
```

## C++ test targets

| CTest target | Source folder | Linked module |
|---|---|---|
| `edge_tts_common_tests` | `tests/common` | `edge_tts::common` |
| `edge_tts_core_tests` | `tests/core` | `edge_tts::core` |
| `edge_tts_serialization_tests` | `tests/serialization` | `edge_tts::serialization` |
| `edge_tts_communication_tests` | `tests/communication` | `edge_tts::communication` |
| `edge_tts_media_tests` | `tests/media` | `edge_tts::media` |
| `edge_tts_subtitle_tests` | `tests/subtitles` | `edge_tts::subtitle` |
| `edge_tts_cli_tests` | `tests/cli` | `edge_tts::cli` |

## Python-based tests

These run automatically with `ctest` when `EDGE_TTS_BUILD_TESTS=ON` and Python 3
is available on `$PATH`.

| CTest target | Script | What it checks |
|---|---|---|
| `edge_tts_docs_tests` | `tests/docs/test_required_reference_docs.py` | `docs/REFERENCE_BEHAVIOR.md` exists, all required headings present, ≥5 reference file paths mentioned |
| `edge_tts_cli_compat_docs_tests` | `tests/docs/test_cli_compatibility_doc.py` | `docs/CLI_COMPATIBILITY.md` exists, both commands mentioned, ≥10 option rows |
| `edge_tts_module_boundary_tests` | `tests/tools/test_module_boundaries.py` | Module include boundaries enforced (see below) |

## Module boundary tests

`edge_tts_module_boundary_tests` runs `tests/tools/test_module_boundaries.py`,
which imports `tools/check_module_boundaries.py` and verifies:

1. **Allowed patterns pass** — all include patterns that are permitted by the
   dependency matrix produce zero violations.
2. **Forbidden patterns fail** — each forbidden include (e.g. `common` including
   `core`, `media` including `communication`) is detected and reported.
3. **Private-header rule** — `apps/` files that include headers via relative
   paths reaching into `src/` are flagged as violations.
4. **Project tree is clean** — the actual `include/`, `src/`, and `apps/`
   sources contain no boundary violations.

Fixture files that demonstrate each violation type live in
`tests/tools/module_boundary_fixtures/`:

```text
allowed/   — files with only permitted includes (must produce no violations)
forbidden/ — files with specific rule violations (must each produce ≥1 violation)
```

Run the boundary checker standalone:
```bash
python3 tools/check_module_boundaries.py
python3 tools/check_module_boundaries.py --verbose   # prints each scanned file
```

See `docs/DEPENDENCY_RULES.md` for the complete allowed-dependency matrix and
the rules the checker enforces.

## Network tests

Tests that call the live Microsoft Edge TTS service are gated by:

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
cmake --build build
ctest --test-dir build -R network
```

Network test targets:

| CTest target | Source file | What it verifies |
|---|---|---|
| `edge_tts_communication_network_tests` | `tests/communication/HttpClientNetworkTests.cpp` | `HttpClient` GET voices endpoint returns HTTP 200 with non-empty JSON; `VoiceService` parses non-empty voice list including `en-US-EmmaMultilingualNeural` |
| `edge_tts_communication_network_tests` | `tests/communication/WebSocketClientNetworkTests.cpp` | `WebSocketClient` + `SynthesisSession` real synthesis: non-empty audio returned, `turn.end` received, word-boundary metadata received when enabled |

Do not enable in CI unless the environment has reliable outbound TLS access to
`speech.platform.bing.com`.

**TLS requirement:** Network tests require ixwebsocket to be built with TLS support.
`EdgeTtsDependencies.cmake` enables `USE_TLS=ON` by default, which uses the system
OpenSSL found by `find_package(OpenSSL)`.  If OpenSSL is absent, configure will fail
with a descriptive error.  To disable TLS (for LAN testing only):

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON -DUSE_TLS=OFF
```

## Compatibility testing

Before implementing any networking, protocol, or text-processing feature, consult
`docs/REFERENCE_BEHAVIOR.md`.  That document describes the exact observed behavior
of the Python `edge-tts` v7.2.8 reference implementation.  Tests for the
communication and serialization layers must be written against the behaviors
documented there, not against assumptions.

## Writing tests

- Use behavior-level tests: test observable outputs, not internal implementation
  details.
- Every module should have at least one test file under `tests/<module>/`.
- New test files must include `"vendor/minigtest/minigtest.hpp"` and use the
  `TEST(Suite, Name)` macro without any `EDGE_TTS_NO_GTEST` guard.
- The `tests/common/test_main.cpp` provides the `main()` function via minigtest
  and must be included in every test target.
- When adding an include that crosses module boundaries, add a row to
  `docs/DEPENDENCY_RULES.md` before writing the code.
