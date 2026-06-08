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
│  Targets: api_network, communication_network,                   │
│           network_smoke (tests/network/)                        │
│  Labels: network, integration (select with ctest -L network)    │
│  What: live Edge TTS service — voice listing, synthesis         │
├─────────────────────────────────────────────────────────────────┤
│  Offline integration tests                                      │
│  Always compiled; always run in default ctest                   │
│  api_tests (CommunicateEndToEndTests.cpp,                       │
│             CommunicateProductionWiringTests.cpp,               │
│             OfflineIntegrationTests.cpp)                        │
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

The three offline integration source files cover complementary areas:

| Source | Focus |
|--------|-------|
| `CommunicateEndToEndTests.cpp` | Happy-path output: MP3 bytes, SRT content, XML escaping, UTF-8, long text, one-shot guarantee |
| `CommunicateProductionWiringTests.cpp` | Production constructor wiring: lazy construction, no placeholder error, save() through fake transport, XML/UTF-8 regressions |
| `OfflineIntegrationTests.cpp` | Frame-level protocol verification: sent frame structure, exact escaping, offset compensation, error propagation, no-audio error |

`OfflineIntegrationTests.cpp` specifically verifies:
- `sent_messages()[0]` has `Path:speech.config` and `Content-Type:application/json`
- `sent_messages()[1]` has `Path:ssml`, `X-RequestId:` (32-char hex), and `<speak>` tags
- Multi-chunk input sends 4 frames (2×speech.config + 2×ssml) and connects twice
- "Tom & Jerry `<test>`" is escaped exactly once (`&amp;`, `&lt;`, `&gt;`) — not double-escaped
- Japanese and Arabic text pass through the encoding pipeline verbatim
- Chunk-2 boundary offsets are shifted by `N*8*10_000_000/48_000` ticks matching the Python reference
- Unknown Path header from the fake server → `ErrorCode::protocol_error`
- `set_receive_error` injection → `ErrorCode::network_error`
- turn.end without audio → `ErrorCode::service_error` with message containing "audio"

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

**WARNING: Real-network tests contact Microsoft Edge TTS servers.**  Only enable
in environments with reliable outbound TLS access to `speech.platform.bing.com`.
Do not run in shared CI unless you accept the dependency on an external service.

Network tests have two independent gates that must both be satisfied:

1. **Compile-time gate** (`EDGE_TTS_ENABLE_NETWORK_TESTS=ON`) — builds the
   network test binaries.  Off by default so environments that must never touch
   the network never even compile these tests.
2. **Run-time gate** (`EDGE_TTS_RUN_NETWORK_TESTS=1` env var) — each test
   body calls `network_enabled()` as its first line and returns immediately when
   the variable is absent, so the test binary still links and CTest shows a pass
   rather than a skip.  Minigtest has no SKIP mechanism; early-return tests
   show as PASSED, not SKIPPED — this is intentional.

### Running all network tests

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
cmake --build build

# Option A — run by CTest label (recommended)
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -L network --output-on-failure

# Option B — run by name pattern
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -R network --output-on-failure
```

### Running only the smoke tests

```bash
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build \
    -R edge_tts_network_smoke_tests --output-on-failure
```

### Skip-gate verification

All network test binaries can be run **without** `EDGE_TTS_RUN_NETWORK_TESTS` to
verify the skip mechanism is working (they must pass immediately with no
assertions fired):

```bash
# No env var — should print PASSED for every test (all skip cleanly):
./build/tests/edge_tts_network_smoke_tests
./build/tests/edge_tts_communication_network_tests
./build/tests/edge_tts_api_network_tests
```

Each network test file includes explicit gate-verification tests
(`RealVoiceListGate.*` and `RealSynthesisGate.*`) that always run and document
the skip contract.

### Network test targets and coverage

| CTest target | Source files | Labels | What it verifies |
|---|---|---|---|
| `edge_tts_communication_network_tests` | `tests/communication/HttpClientNetworkTests.cpp`, `WebSocketClientNetworkTests.cpp` | `network`, `integration` | `HttpClient` GET voices returns HTTP 200; `VoiceService` parses non-empty list; `SynthesisSession` synthesis: non-empty audio, `turn.end`, word-boundary metadata |
| `edge_tts_api_network_tests` | `tests/api/CommunicateNetworkTests.cpp` | `network`, `integration` | `api::Communicate` end-to-end: `stream_sync()` returns audio, `save()` writes non-empty MP3, SRT written with word-boundary mode, proxy option forwarded |
| `edge_tts_network_smoke_tests` | `tests/network/RealVoiceListTests.cpp`, `RealSynthesisSmokeTests.cpp` | `network`, `integration` | **Dedicated smoke tests**: voice-list field completeness (ShortName, Gender, Locale); default voice presence; locale/gender filters; short-phrase synthesis; word-boundary chunks and SRT; alternative voice accepted; temp-file cleanup |

### Dedicated smoke-test files (`tests/network/`)

`tests/network/RealVoiceListTests.cpp` covers:
- HTTP 200 from the voices endpoint
- Non-empty voice list
- Every voice has non-empty `ShortName`, `Gender` (not `unknown`), `Locale`, `Name`, `FriendlyName`
- `en-US-EmmaMultilingualNeural` (reference default) is present with correct fields
- Locale filter (`en-US`) returns only matching voices
- Gender filters return consistent results
- Multiple locales exist in the full list

`tests/network/RealSynthesisSmokeTests.cpp` covers:
- `SynthesisSession` + `WebSocketClient` direct synthesis: audio bytes, non-zero size, `turn.end` termination
- Word-boundary mode: `BoundaryChunk` events, non-empty text, non-negative offsets
- `api::Communicate` production constructor wires real stack (not a stub)
- `save()` writes non-empty MP3
- `save()` with word-boundary writes non-empty SRT
- `stream_sync()` returns both `AudioChunk` and `BoundaryChunk`
- Alternative voice (`en-GB-RyanNeural`) accepted by service

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

### CLI test coverage matrix

`EdgeTtsArgumentParserTests.cpp` — parser behavior (stateless, no I/O):

| Area | Tests |
|------|-------|
| No args | `NoArgsIsError` |
| `--help` / `-h` | `LongHelpReturnsHelp`, `ShortHelpReturnsHelp`, `HelpShortCircuitsInvalidArgs`, `HelpTextContainsOptions`, `HelpTextContainsAllDocumentedOptions`, `HelpTextMentionsNegativeValueSyntax` |
| `--version` | `VersionReturnsVersion`, `VersionStringContainsProjectName`, `VersionStringContainsVersionNumber` |
| `--text` / `-t` | `TextLongForm`, `TextShortForm`, `TextEqualsForm`, `TextMissingValueIsError`, `ShortTextMissingValueIsError` |
| `--file` / `-f` | `FileLongForm`, `FileShortForm`, `FileStdinDash`, `FileDevStdin`, `FileMissingValueIsError`, `ShortFileMissingValueIsError` |
| `--list-voices` / `-l` | `ListVoicesLong`, `ListVoicesShort`, `ListVoicesWithProxy` |
| Mutual exclusion | `TextAndFileConflict`, `TextAndListVoicesConflict`, `FileAndListVoicesConflict` |
| `--voice` / `-v` | `VoiceLongForm`, `VoiceShortForm`, `VoiceEqualsForm`, `VoiceMissingValueIsError`, `ShortVoiceMissingValueIsError` |
| `--rate` | `Rate`, `RateEqualsNegative`, `RateMissingValueIsError`, `RateNegativeWithSpaceIsError` |
| `--volume` | `Volume`, `VolumeEqualsNegative`, `VolumeMissingValueIsError`, `VolumeNegativeWithSpaceIsError` |
| `--pitch` | `Pitch`, `PitchEqualsNegative`, `PitchMissingValueIsError`, `PitchNegativeWithSpaceIsError` |
| `--write-media` | `WriteMedia`, `WriteMediaDashIsStdout`, `WriteMediaEqualsForm`, `WriteMediaMissingValueIsError` |
| `--write-subtitles` | `WriteSubtitles`, `WriteSubtitlesDashIsStderr`, `WriteSubtitlesMissingValueIsError` |
| `--proxy` | `Proxy`, `ProxyEqualsForm`, `ProxyEmptyStringIsParseError`, `ProxyMissingSchemeIsParseError`, `ProxyBareHostnameIsParseError`, `ProxyWithHttpSchemeIsAccepted`, `ProxyWithHttpsSchemeIsAccepted`, `ProxyParseErrorMessageIsDescriptive`, `ProxyMissingValueIsError` |
| Unknown / positional | `UnknownLongOptionIsError`, `UnknownShortOptionIsError`, `FormatOptionIsNotSupported`, `PositionalArgumentIsError`, `PositionalAfterOptionsIsError` |
| Defaults | `DefaultVoice*`, `DefaultRate*`, `DefaultVolume*`, `DefaultPitch*`, `DefaultWriteMedia*`, `DefaultWriteSubtitles*`, `DefaultProxy*`, `DefaultListVoices*` |
| argc/argv interface | `ArgcArgvSkipsArgv0` |

`EdgeTtsCommandDispatcherTests.cpp` — dispatcher behavior (with injected seams):

| Area | Tests |
|------|-------|
| list-voices: success | `ListVoicesCallsVoiceService`, `ListVoicesExitCodeSuccess`, `ListVoicesEmptyListSucceeds` |
| list-voices: formatted output | `ListVoicesPrintsFormattedTable`, `ListVoicesSortedInOutput` |
| list-voices: service error | `ListVoicesServiceErrorPrintsToStderr`, `ListVoicesServiceErrorDoesNotPrintToStdout` |
| Text synthesis | `TextSynthesisCallsFactory`, `TextSynthesisExitCodeSuccess` |
| File synthesis | `FileSynthesisLoadsFile`, `FileSynthesisMissingFileReturnsError` |
| stdin (`--file=-`) | `FileDashReadsFromStdin`, `FileDevStdinReadsFromStdin` |
| TTS config forwarding | `VoiceForwardedToFactory`, `RateForwardedToFactory`, `VolumeForwardedToFactory`, `PitchForwardedToFactory` |
| Proxy forwarding | `ProxyIsForwardedToFactory`, `EmptyProxyIsForwardedToFactory` |
| Audio → stdout | `AudioWrittenToStdoutWhenNoWriteMedia`, `AudioWrittenToStdoutWhenWriteMediaIsDash` |
| Audio → file | `WriteMediaWritesAudioFile`, `WriteMediaFileDoesNotPolluteSstdout` |
| Binary output correctness | `BinaryAudioBytesNotCorrupted`, `BinaryAudioBytesNotCorruptedWhenRoutedToFile` |
| Subtitles → stderr | `WriteSubtitlesDashWritesToStderr` |
| Subtitles → file | `WriteSubtitlesToFile`, `WriteSubtitlesFileErrorReturnsFailure`, `WriteSubtitlesFileErrorIncludesFilenameInStderr` |
| No subtitles | `NoSubtitlePathProducesNoSrtOutput` |
| Synthesis error | `SynthesisErrorPrintsToStderr`, `SynthesisErrorDoesNotWriteToStdout` |
| help / version / error dispatch | `HelpPrintsToStdoutAndReturns0`, `VersionPrintsToStdoutAndReturns0`, `ErrorActionPrintsToStderrAndReturns2` |
| Media file write error | `WriteMediaFileErrorReturnsFailure`, `WriteMediaFileErrorIncludesFilenameInStderr` |
| Proxy runtime error | `ProxyUnsupportedYieldsExitCode1AndErrorOnStderr`, `ProxyIsNotSilentlyIgnored` |
| Proxy credential redaction | `ProxyCredentialNotExposedInStderr` |
| SubMaker type conflict | `SubtitleFeedTypeMismatchReturnsError`, `SubtitleFeedErrorPrintsMessageToStderr` |
| TTY warning | `TtyWarningPrintedToStderr`, `TtyWarningAllowsSynthesisAfterEnter`, `TtyWarningCancelsOnEof`, `TtyWarningNotShownWhen*` (4 variants) |

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
