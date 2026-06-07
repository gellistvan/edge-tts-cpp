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

## Testing pyramid

```
┌─────────────────────────────────────────────────────────────────┐
│  Real-network integration tests                                 │
│  Gate 1 (compile): EDGE_TTS_ENABLE_NETWORK_TESTS=ON            │
│  Gate 2 (runtime): EDGE_TTS_RUN_NETWORK_TESTS=1                │
│  Targets: api_network, communication_network                    │
│  What: live Edge TTS service — voice listing, synthesis         │
├─────────────────────────────────────────────────────────────────┤
│  Offline integration tests                                      │
│  Always compiled; always run in default ctest                   │
│  api_tests (CommunicateEndToEndTests.cpp,                       │
│             CommunicateProductionWiringTests.cpp)               │
│  Full stack: Communicate → SynthesisSession → EdgeProtocol      │
│  → FakeWebSocketClient → FileWriter (deterministic, no network) │
├─────────────────────────────────────────────────────────────────┤
│  Unit tests (all other targets)                                 │
│  Isolated per-module, fast, no I/O, no network                  │
└─────────────────────────────────────────────────────────────────┘
```

**Unit tests** cover one module at a time with fakes for their dependencies.
No outbound connections, no file I/O beyond temp files.

**Offline integration tests** wire the full `api::Communicate` stack end-to-end
using `FakeWebSocketClient` — deterministic, no outbound connections, always run in CI.
All `Communicate` objects in normal tests are constructed with the `SynthesizerFn`
injection constructor; the production 2/3-arg constructors (which create a real
`WebSocketClient`) are tested only via structural assertions that do not call
`stream_sync()` or `save()`.

**Real-network integration tests** call the live Edge TTS service and must be opted
in explicitly via **two independent gates** (see the Network tests section below).
The default `ctest` invocation never runs these tests, is deterministic, and requires
no internet access.

### Network test isolation rules

These rules are enforced by `tests/tools/test_network_hygiene.py` (run as
`edge_tts_network_hygiene_tests` in the default `ctest` suite):

1. **No production Communicate in normal tests.** A normal test file must not
   call `stream_sync()` or `save()` on a `Communicate` object that was constructed
   without a `SynthesizerFn` injection argument.  The production 2-arg and 3-arg
   constructors wire a real `WebSocketClient` and would attempt TLS connections.

2. **Network files are named `*Network*.cpp`.** Only files containing
   `Network` in their name may reference `EDGE_TTS_RUN_NETWORK_TESTS` or
   `network_enabled()` in code (comments are exempt).

3. **Network binaries are compile-gated.** Every `*NetworkTests.cpp` source in
   `tests/CMakeLists.txt` must appear inside an
   `if(EDGE_TTS_ENABLE_NETWORK_TESTS)` block.

4. **Each network test checks the runtime gate.** Every `TEST(...)` body in a
   `*NetworkTests.cpp` file must call `network_enabled()` within its first 6
   executable lines and return immediately when false.

## C++ test targets

| CTest target | Source folder | Linked module |
|---|---|---|
| `edge_tts_common_tests` | `tests/common` | `edge_tts::common` |
| `edge_tts_core_tests` | `tests/core` | `edge_tts::core` |
| `edge_tts_serialization_tests` | `tests/serialization` | `edge_tts::serialization` |
| `edge_tts_communication_tests` | `tests/communication` | `edge_tts::communication` |
| `edge_tts_media_tests` | `tests/media` | `edge_tts::media` |
| `edge_tts_subtitle_tests` | `tests/subtitles` | `edge_tts::subtitle` |
| `edge_tts_api_tests` | `tests/api` | `edge_tts::api`, `edge_tts::cli` |
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

Network tests have two independent gates that must both be satisfied:

1. **Compile-time gate** (`EDGE_TTS_ENABLE_NETWORK_TESTS=ON`) — builds the
   network test binaries.  Off by default so environments that must never touch
   the network never even compile these tests.
2. **Run-time gate** (`EDGE_TTS_RUN_NETWORK_TESTS=1` env var) — each test
   body checks this at startup and returns immediately when unset, so the test
   binary still links and CTest shows a pass rather than a skip.

To run all network tests:

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
cmake --build build
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -R network --output-on-failure
```

Network test targets:

| CTest target | Source file | What it verifies |
|---|---|---|
| `edge_tts_communication_network_tests` | `tests/communication/HttpClientNetworkTests.cpp` | `HttpClient` GET voices endpoint returns HTTP 200 with non-empty JSON; `VoiceService` parses non-empty voice list including `en-US-EmmaMultilingualNeural` |
| `edge_tts_communication_network_tests` | `tests/communication/WebSocketClientNetworkTests.cpp` | `WebSocketClient` + `SynthesisSession` real synthesis: non-empty audio returned, `turn.end` received, word-boundary metadata received when enabled |
| `edge_tts_api_network_tests` | `tests/api/CommunicateNetworkTests.cpp` | `api::Communicate` end-to-end: stream_sync() returns non-empty audio, save() writes non-empty MP3, SRT written when word-boundary enabled, bogus proxy causes transport failure |

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

## CLI tests — avoiding real network calls

`edge_tts_cli_tests` (`tests/cli/`) tests the full CLI dispatch path without any
real network calls or real process streams. This is achieved through constructor
injection in `EdgeTtsCommandDispatcher`:

| Seam | Test injection | Production value |
|------|---------------|-----------------|
| `VoiceServiceFn` | Lambda returning a fixed `vector<Voice>` or error | `VoiceService::list_voices()` |
| `CommunicateFactory` | Lambda creating `Communicate` with a fake `SynthesizerFn` | `Communicate{text, cfg, opts}` |
| `std::ostream& out` | `std::ostringstream` | `std::cout` |
| `std::ostream& err` | `std::ostringstream` | `std::cerr` |
| `std::istream& in` | `std::istringstream` | `std::cin` |
| `TtyCheckFn` | Lambda returning `true` or `false` | `isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)` |

The fake `Communicate` is created via the `SynthesizerFn` injection constructor:
```cpp
Communicate(text, cfg, opts,
    [chunks](const TtsConfig&, std::span<const std::string>)
        -> Result<vector<TtsChunk>> {
        return Result<vector<TtsChunk>>::ok(chunks);
    })
```

This lets CLI tests verify exact stdout/stderr/file bytes, exit codes, and error
messages without any outbound network requests.

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
