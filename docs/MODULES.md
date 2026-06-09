# Module Ownership Reference

This document describes what each module owns, what it may depend on, and what
it must never import.

For the enforced CMake dependency matrix see `docs/DEPENDENCY_RULES.md`.

---

## CMake helpers

Three cmake helper files manage module and test registration:

| File | Purpose |
|------|---------|
| `cmake/EdgeTtsAddModule.cmake` | `edge_tts_add_module()` — registers a library module with correct include paths, C++20 requirement, and PRIVATE compile options |
| `cmake/EdgeTtsAddTest.cmake` | `edge_tts_add_module_test()` — registers a test executable with minigtest include path and `add_test` registration |
| `cmake/EdgeTtsCompilerOptions.cmake` | Creates the `edge_tts_compile_options` INTERFACE target carrying all project warning flags |

---

## PUBLIC / PRIVATE / INTERFACE dependency rules

These rules govern `target_link_libraries` choices for all module targets.
They ensure that consumers linking `edge_tts::tts` receive exactly what they need
and nothing more.

### Rule 1 — PUBLIC: use when consumers need the dep's types or headers

Declare a dependency `PUBLIC` when the dependent target's **public headers**
include or name types from the dependency.  Example: `edge_tts::core` headers
reference `edge_tts::common` types, so `core` lists `common` as `PUBLIC_DEPS`.

### Rule 2 — PRIVATE: use when the dep is implementation-only

Declare a dependency `PRIVATE` when it is used exclusively in `.cpp` files
and no type or constant from it appears in any public header under `include/`.
Example: `nlohmann_json` is used only in `VoiceJsonParser.cpp`; it is PRIVATE.

For **third-party static libraries** that are PRIVATE, if the consumer's final
link step must still include that library's symbols, use `$<LINK_ONLY:...>` in
`INTERFACE_LINK_LIBRARIES` to propagate the link dependency without propagating
include directories or compile definitions.  `ixwebsocket` follows this pattern:
`edge_tts_communication` links it PRIVATE (for compilation), then adds
`$<LINK_ONLY:ixwebsocket>` to INTERFACE_LINK_LIBRARIES so static consumers
automatically get it on their link line.

### Rule 3 — Warning flags are NEVER propagated

All project warning flags live in `edge_tts_compile_options` (INTERFACE target).
Compiled modules inherit these flags via a generator-expression property read
(`$<TARGET_PROPERTY:edge_tts_compile_options,...>`), which does NOT cause the
`edge_tts_compile_options` target to appear in `LINK_LIBRARIES`.  Consumers
compile with their own flag set; `-Werror` and `-Wall` from edge-tts-cpp do
not affect them.

### Rule 4 — Include directories

Every module sets `BUILD_INTERFACE` to the repo's `include/` tree and
`INSTALL_INTERFACE` to `include` (relative, resolved against `_IMPORT_PREFIX`
in installed packages).  Submodule source paths never appear in
`INTERFACE_INCLUDE_DIRECTORIES` of installed targets.

### Rule 5 — cxx_std_20 is INTERFACE / PUBLIC

`edge_tts::tts` declares `cxx_std_20` as an INTERFACE compile feature.
Consumers inherit this requirement automatically; they do not need to add
`target_compile_features(... cxx_std_20)` unless they also target C++20 APIs
independently.

---

## `edge_tts::common`

**CMake target:** `edge_tts_common` / `edge_tts::common`
**Test target:** `edge_tts_common_tests`
**Headers:** `include/edge_tts/common/`
**Sources:** `src/common/Error.cpp`

| File | Description |
|------|-------------|
| `Errors.hpp` | Exception hierarchy rooted at `Exception : std::runtime_error`. Throw-based error types for API boundary violations: `ConfigurationError`, `NetworkError`, `ProtocolError`, `AudioError`, `SubtitleError`. |
| `Error.hpp` | `ErrorCode` enum + value-type `Error` class. Used with `Result<T>` for recoverable runtime failures. `to_string(ErrorCode)` for logging. |
| `Result.hpp` | `Result<T>` and `Result<void>` — lightweight result types built on `std::variant<T, Error>`. `BadResultAccess` thrown on misuse. |
| `Clock.hpp` | `IClock` abstract interface (UTC `time_point`), `SystemClock` (wraps `system_clock::now()`), `FixedClock` (deterministic test double). No protocol-specific epoch conversions — those live in the communication/serialization layer. |
| `Hex.hpp` | `hex_encode_lower(span<byte>)`, `hex_encode_upper(span<byte>)`, `is_hex(string_view)`. Used by `IdGenerator` and the communication/DRM layer for encoding. |
| `Utf8.hpp` | `is_continuation(char)` (constexpr), `safe_boundary(sv, pos)` (constexpr, backward compat). Full API: `is_valid_utf8(sv)` (rejects overlong, surrogates, truncated, >U+10FFFF), `previous_code_point_boundary(sv, i)`, `next_code_point_boundary(sv, i)`, `split_utf8_by_byte_limit(sv, max_bytes)`. `split_utf8_by_byte_limit` is the canonical source for safe UTF-8 slicing; used by `serialization::TextChunker`. |
| `IdGenerator.hpp` | `IdGenerator` class: `uuid_v4()` (36-char hyphenated lowercase UUID v4), `uuid_v4_without_hyphens()` (32-char, matches Python `uuid.uuid4().hex`), `random_32_hex()` (32-char lowercase, matches Python `secrets.token_hex(16)`). Edge-specific header assignments (ConnectionId, X-RequestId, MUID) are in the communication layer. |
| `Expected.hpp` | Generic `Expected<T,E>` / `Unexpected<E>` for cases that need a custom error type. |
| `Utf8.hpp` | Constexpr UTF-8 byte utilities: `is_continuation(char)`, `safe_boundary(string_view, pos)`, `is_valid(string_view)`. Used by `serialization::TextChunker` to avoid splitting mid-sequence. |

**Allowed dependencies:** none (foundational, must stay zero-dep).

### `IClock` contract

`IClock::now()` returns a `std::chrono::system_clock::time_point` in UTC —
the same epoch as Python's `datetime.now(timezone.utc).timestamp()`.

**Do not** add Edge protocol constants (Windows epoch offset, 100 ns tick
conversion, 5-minute round-down) to `Clock.hpp`.  Those belong in the token
generation code.  See `docs/PROTOCOL_NOTES.md` for the full timing reference.

### Error strategy

Two error propagation styles are used:

| Style | When to use | Types |
|-------|-------------|-------|
| Exceptions (`throw`) | Programmer errors at API boundaries — invalid constructor arguments, pre-condition violations.  These are bugs, not recoverable conditions. | `ConfigurationError`, `NetworkError`, `ProtocolError`, `AudioError`, `SubtitleError` (all final, inherit `Exception`) |
| `Result<T>` (return value) | Runtime failures that the caller can handle — I/O errors, network failures, parse errors, service refusals. | `Result<T>`, `Result<void>`, `Error`, `ErrorCode` |

**Python → C++ ErrorCode mapping:**

| Python exception | `ErrorCode` | Notes |
|-----------------|-------------|-------|
| `TypeError`, `ValueError` | `invalid_argument` | Validation failures on caller input |
| `RuntimeError` (stream reuse) | `invalid_state` | Operation not permitted in current state |
| `WebSocketError` | `network_error` | Transport-level failure |
| `SkewAdjustmentError` | `network_error` | Clock skew; recoverable via retry |
| `UnknownResponse` | `protocol_error` | Unexpected wire message type |
| `UnexpectedResponse` | `protocol_error` | Well-formed but semantically unexpected |
| `NoAudioReceived` | `service_error` | Service returned no audio data |
| HTTP 4xx/5xx | `service_error` | Service refused the request |
| File I/O failure | `io_error` | Local filesystem read/write |
| JSON / header decode failure | `parse_error` | Could not interpret response body |
| Connect/receive timeout | `timeout` | Timeout parameters exceeded |
| ffmpeg/ffplay failure | `external_process_failed` | Non-zero process exit code |

---

## `edge_tts::core`

**CMake target:** `edge_tts_core` / `edge_tts::core`
**Test target:** `edge_tts_core_tests`
**Headers:** `include/edge_tts/core/`

| File | Description |
|------|-------------|
| `Chunk.hpp` | `BoundaryType` enum (`WordBoundary`, `SentenceBoundary`). `AudioChunk` (raw MP3 bytes). `BoundaryChunk` (timing event: offset/duration in 100 ns ticks, text). `TtsChunk = std::variant<AudioChunk, BoundaryChunk>`. |
| `TtsChunk.hpp` | Compatibility shim — `#include`s `Chunk.hpp`. |
| `TtsConfig.hpp` | `TtsConfig` struct (voice, rate, volume, pitch, boundary). `validate()` normalizes the voice field and throws `ConfigurationError` on invalid syntax. `normalize_voice_name(string_view)` free function. |
| `Voice.hpp` | `VoiceGender` enum (`Female`, `Male`, `Unknown`). `Voice` struct (name, short_name, gender, locale, styles) with `operator==`/`operator!=`. |
| `OutputFormat.hpp` | `OutputFormat` — validated audio format type. `default_format()` returns `"audio-24khz-48kbitrate-mono-mp3"` (the only format used by the Python reference). `from_string()` rejects empty strings and unknown formats. No arbitrary format strings permitted. |
| `Voice.hpp` | `VoiceGender {unknown, female, male}`, `Voice` struct (all fields from Python `Voice` TypedDict: `name`, `short_name`, `gender`, `locale`, `friendly_name`, `status`, `suggested_codec`, `content_categories`, `voice_personalities`, `language`). `voice_gender_from_string()` parses `"Female"`/`"Male"` (case-sensitive). `to_string(VoiceGender)` returns exact Python wire strings. |

**Allowed dependencies:** `edge_tts::common`.

**Forbidden:** networking, JSON parsing, serialization, filesystem, process execution.

### `TtsConfig` validation rules

`TtsConfig::validate()` is non-const.  It:

1. Accepts `voice` in either the short (`en-US-EmmaMultilingualNeural`) or full
   (`Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)`)
   form and normalizes to the full form.
2. Rejects `voice` strings that match neither form.
3. Validates `rate` against `^[+-]\d+%$`.
4. Validates `volume` against `^[+-]\d+%$`.
5. Validates `pitch` against `^[+-]\d+Hz$`.

After `validate()` returns, `voice` holds the normalized form.  Subsequent calls
are idempotent.

---

## `edge_tts::serialization`

**CMake target:** `edge_tts_serialization` / `edge_tts::serialization`
**Test target:** `edge_tts_serialization_tests`
**Headers:** `include/edge_tts/serialization/`

Owns the Edge TTS WebSocket protocol format: SSML message construction,
speech-config payloads, protocol/connection token metadata, and JSON parsing
of the voice-list response.

| File | Description |
|------|-------------|
| `SsmlBuilder.hpp` | Builds SSML `<speak>` documents from `TtsConfig` + raw text. |
| `XmlEscaper.hpp` | `xml_escape` / `xml_unescape` matching `xml.sax.saxutils`. |
| `TextNormalizer.hpp` | UTF-8 validation + control-character replacement. |
| `TextChunker.hpp` | `TextChunker` + `TextChunkerOptions` — the Edge SSML chunker: normalize UTF-8, XML-escape, split at the 4096-byte escaped limit (sentence/word/UTF-8 boundary preference), protect XML entities. Returns chunks ready for `<prosody>` embedding. |
| `VoiceJsonParser.hpp` | Parses the voice-list JSON array into `std::vector<core::Voice>`. No HTTP dependency. |
| `ProtocolMessage.hpp` | `ProtocolMessage` struct: `vector<pair<string,string>>` headers + string body. Header lookup via `header(name)`. |
| `ProtocolParser.hpp` | Parses an Edge TTS WebSocket text frame (`\r\n\r\n`-delimited) into a `ProtocolMessage`. |
| `ProtocolSerializer.hpp` | Serializes a `ProtocolMessage` into an Edge TTS WebSocket text frame string. |
| `MetadataJsonParser.hpp` | Parses `audio.metadata` JSON payloads into `vector<core::BoundaryChunk>`. Handles SessionEnd skip, unknown type errors, and XML-unescapes `Text`. |

**Third-party dependency:** `nlohmann/json` (header-only, submodule at
`submodules/json`).  Linked to `edge_tts_serialization` and
`edge_tts_communication` via the optional block in the top-level
`CMakeLists.txt`.

**Allowed dependencies:** `edge_tts::core`, `edge_tts::common`, `nlohmann_json::nlohmann_json`.

**Forbidden:** networking (HTTP/WebSocket I/O).

### `VoiceJsonParser` — voice list JSON contract

`VoiceJsonParser::parse(string_view json)` returns
`Result<vector<core::Voice>>`.

| Condition | Behaviour |
|-----------|-----------|
| Malformed JSON | `Result::fail` with `ErrorCode::parse_error` |
| Root is not an array | `Result::fail` with `ErrorCode::parse_error` |
| Array element is not an object | `Result::fail` with `ErrorCode::parse_error` |
| Missing required field | `Result::fail` with `ErrorCode::parse_error` |
| Unrecognised `Gender` value | `Result::fail` with `ErrorCode::parse_error` |
| Missing `VoiceTag` | Defaults `content_categories` and `voice_personalities` to `[]` |
| Missing `VoiceTag` sub-lists | Each defaults to `[]` independently |
| Unknown fields | Silently ignored |
| Ordering | Wire order preserved; CLI layer is responsible for sorting by `ShortName` |

---

## `edge_tts::subtitle`

**CMake target:** `edge_tts_subtitle` / `edge_tts::subtitle`
**Test target:** `edge_tts_subtitle_tests`
**Headers:** `include/edge_tts/subtitles/`
**Sources:** `src/subtitles/`

Owns timing conversion, subtitle cue modeling, and SRT composition.

| File | Description |
|------|-------------|
| `SubtitleTime.hpp` | `SubtitleTime` — wraps millisecond count. `from_edge_ticks(int64_t)` converts 100 ns ticks (`ticks / 10'000`) with integer truncation. `to_srt_timestamp()` formats `HH:MM:SS,mmm`. Rejects negative ticks. |
| `SubtitleCue.hpp` | `SubtitleCue` — plain struct: `SubtitleTime start`, `SubtitleTime end`, `std::string text`. |
| `SrtComposer.hpp` | `SrtComposer::compose(span<const SubtitleCue>)` — sorts by `(start, end)`, skips empty/whitespace text and `start >= end`, applies `make_legal_content` text cleanup, emits `{idx}\n{start} --> {end}\n{content}\n\n` blocks. |
| `SubMaker.hpp` | `SubMaker` — accumulates `BoundaryChunk` events into `SubtitleCue` values. `feed()` enforces type consistency; `to_srt()` composes via `SrtComposer`; `clear()` resets state. |

### SrtComposer reference contract

| Behaviour | Reference source |
|-----------|-----------------|
| Sort order | `(start, end)` ascending — `Subtitle.__lt__` in srt_composer.py |
| Skip: `start >= end` | `SUBTITLE_SKIP_CONDITIONS[2]` |
| Skip: empty/whitespace content | `SUBTITLE_SKIP_CONDITIONS[0]`: `not sub.content.strip()` |
| Text cleanup | `make_legal_content`: strip leading/trailing `\n`; collapse `\n\n+` → `\n` |
| Block format | `"{idx}\n{start} --> {end}\n{content}\n\n"` (LF only) |
| Index after skip | Skipped cues do not consume an index — contiguous from 1 |
| Fixture | `tests/subtitles/fixtures/basic.srt` |

**Allowed dependencies:** `edge_tts::core`, `edge_tts::common` (transitively via core).

---

## `edge_tts::media`

**CMake target:** `edge_tts_media` / `edge_tts::media`
**Test target:** `edge_tts_media_tests`
**Headers:** `include/edge_tts/media/`

Owns the `ffmpeg`/`ffplay` process boundary.  Converts or plays audio by
spawning system executables — no direct linking to FFmpeg libraries.

**Allowed dependencies:** `edge_tts::common`.

**Forbidden:** TTS configuration logic, protocol parsing.

| File | Description |
|------|-------------|
| `ProcessRunner.hpp` | `ProcessCommand` (executable + argument list, no shell string), `ProcessResult` (exit_code, stdout_text, stderr_text), `IProcessRunner` (injectable interface), `ProcessRunner` (POSIX fork+execvp, no `system()` or shell) |
| `AudioConverter.hpp` | `IAudioConverter` — pure virtual interface: `play_mp3(path)`, `convert(input, output)`. Both return `Result<void>`. |
| `ExecutableDiscovery.hpp` | `ExecutableDiscovery::find_on_path(name, path_env)` — PATH scanner used to locate `ffmpeg`/`ffplay` without executing any process. Returns `optional<path>`. |
| `FfmpegAudioConverter.hpp` | `FfmpegAudioConverter` — concrete `IAudioConverter`. Uses `ExecutableDiscovery` to find `ffmpeg` (for `convert()`) and `ffplay` (for `play_mp3()`) then dispatches via an injected `IProcessRunner`. No FFmpeg library is linked. |

---

## `edge_tts::communication`

**CMake target:** `edge_tts_communication` / `edge_tts::communication`
**Test target:** `edge_tts_communication_tests`
**Headers:** `include/edge_tts/communication/`

WebSocket transport infrastructure, voice-list service, and synthesis
orchestration.  Does NOT own the public `Communicate` facade — that lives
in `edge_tts::api` above.  See `edge_tts::api` for the public API.

| File | Description |
|------|-------------|
| `EdgeServiceConfig.hpp` | All hard-coded Edge TTS constants (endpoints, token, version, headers, paths). `default_edge_service_config()` is the single source of truth. |
| `EdgeTokenProvider.hpp` | Generates `Sec-MS-GEC` tokens from injectable `IClock` + `EdgeServiceConfig`. Uses `common::sha256_hex_upper`. |
| `ConnectionMetadata.hpp` | `ConnectionMetadata` struct (connection_id + request_id, both 32-char lowercase hex UUID v4 without hyphens). `ConnectionMetadataFactory` wraps `IdGenerator`. |
| `HttpTypes.hpp` | `HttpRequest` and `HttpResponse` plain data types. |
| `IHttpClient.hpp` | Pure virtual HTTP transport boundary. `send(HttpRequest)→Result<HttpResponse>`. |
| `EdgeRequestHeaders.hpp` | `build_websocket_headers(config, ids)` → `vector<pair<string,string>>` (7 headers incl. Cookie/MUID); `build_voice_list_headers(config, ids)` → `map<string,string>` (5 headers incl. Cookie/MUID). Both match Python `DRM.headers_with_muid(WSS_HEADERS)` / `DRM.headers_with_muid(VOICE_HEADERS)` exactly. |
| `VoiceService.hpp` | Fetches and parses the Edge TTS voice list. Injects `IHttpClient`, `VoiceJsonParser`, `IdGenerator`, and `EdgeTokenProvider`. Retries once on HTTP 403 with clock-skew correction. `VoiceFilter` applies client-side locale/gender/short_name filtering. |
| `EdgeProtocol.hpp` | Builds outgoing WebSocket text frames (`build_speech_config_frame`, `build_ssml_frame`) and parses incoming frames (`parse_incoming`). The single production path for all protocol framing. |
| `IWebSocketClient.hpp` | Pure virtual WebSocket transport boundary. |
| `WebSocketClient.hpp` | `WebSocketClient` — ixwebsocket-backed `IWebSocketClient` (Pimpl; no ixwebsocket symbols in the public header). |
| `SynthesisSession.hpp` | Per-chunk WebSocket synthesis lifecycle: connect → speech.config → SSML → receive loop → boundary offset compensation. |
| `RetryPolicy.hpp` | Configurable `RetryPolicy` used by `SynthesisSession` on DRM errors. |
| `HttpDate.hpp` | Parses the HTTP `Date` header for clock-skew correction. |

**Allowed dependencies:** `common`, `core`, `serialization`.

**Forbidden:** `subtitle`, `media` — `communication` is pure transport
orchestration (WebSocket/HTTP framing, session lifecycle, DRM token).  Subtitle
accumulation and audio conversion are `api`-layer concerns.  The public
`Communicate` facade lives in `api`, above `communication`.

---

## `edge_tts::api`

**CMake target:** `edge_tts_api` / `edge_tts::api`
**Test target:** `edge_tts_api_tests` (offline integration — uses `FakeWebSocketClient` from `edge_tts_test_support`; real-network tests gated behind `EDGE_TTS_ENABLE_NETWORK_TESTS=ON`)
**Headers:** `include/edge_tts/api/`
**Sources:** `src/api/`

Public user-facing facade module.  This is the only module end-users and
the CLI layer should depend on for synthesis.

| File | Description |
|------|-------------|
| `Communicate.hpp` | `Communicate` — public synthesis facade. Mirrors the Python `Communicate` class from `communicate.py`. Orchestrates `serialization::TextChunker`, `SynthesisSession`, subtitle generation, and audio saving. Production constructors compose the full networking stack (WebSocketClient, SynthesisSession, EdgeTokenProvider, EdgeProtocol) lazily — no network I/O occurs until `stream_sync()` or `save()` is called. |

**Rationale for a separate module:** `Communicate` needs `SynthesisSession` from
`communication`, `SubMaker` from `subtitle`, and `media` for audio saving.
Placing it above `communication` keeps the transport infrastructure clean and
avoids a fat `communication` module that also owns public API semantics.

**Namespace:** `edge_tts::api`

**Allowed dependencies:** `common`, `core`, `serialization`, `subtitle`, `media`,
`communication`.

**Forbidden:** `cli` or anything above `api` in the dependency graph.

---

## `edge_tts::cli`

**CMake target:** `edge_tts_cli` / `edge_tts::cli`
**Test target:** `edge_tts_cli_tests`
**Headers:** `include/edge_tts/cli/`
**Sources:** `src/cli/`

CLI argument parsing and application-level plumbing shared between the
`edge-tts` and `edge-playback` executables.  Uses a hand-rolled token
parser (`EdgeTtsArgumentParser`, `PlaybackArgumentParser`) that mirrors the
Python `argparse` option surface exactly.

**Allowed dependencies (PUBLIC):** `edge_tts::api`, `edge_tts::media`
(PlaybackCommandDispatcher public header exposes `IAudioConverter`).

**Allowed dependencies (PRIVATE):** `edge_tts::subtitle` (EdgeTtsCommandDispatcher.cpp
uses `SubMaker`; not exposed in any public header).

**Transitive via api:** `core`, `common`, `serialization`, `communication`, `subtitle`.

**Forbidden:** direct `#include` of `communication` or `serialization` headers —
cli must reach those only through the `api` facade, never directly.

---

## `edge_tts_test_support` (test-only)

**CMake target:** `edge_tts_test_support` (STATIC, defined in `tests/CMakeLists.txt`)
**Headers:** `tests/support/edge_tts/` (NOT under `include/` — not exported)
**Sources:** `tests/support/Fake*.cpp`

In-memory test doubles for the injectable production interfaces.  Only test
targets may link against this target.  Production modules must not.

| Header | Description |
|--------|-------------|
| `tests/support/edge_tts/communication/FakeWebSocketClient.hpp` | In-memory `IWebSocketClient`: error injection, message queuing, state inspection |
| `tests/support/edge_tts/communication/FakeHttpClient.hpp` | In-memory `IHttpClient`: configurable response, request capture, error injection, send count |
| `tests/support/edge_tts/media/FakeProcessRunner.hpp` | In-memory `IProcessRunner`: configurable result, error injection, call count — no POSIX deps |

**Include path:** `tests/support/` is the include root, so existing includes such
as `"edge_tts/communication/FakeWebSocketClient.hpp"` resolve from the test
executable's include directory list.  Production library targets do not have
`tests/support/` in their include paths.

**Enforcement:** three checks in `tests/tools/test_repository_hygiene.py` prevent
regression:
- `test_fake_headers_not_in_public_include` — fails if any `Fake*.hpp` appears under `include/edge_tts/`
- `test_fake_sources_not_in_production_src` — fails if any `Fake*.cpp` appears under `src/`
- `test_production_cmake_does_not_compile_fakes` — fails if root `CMakeLists.txt` lists a `Fake*.cpp` source in a production target

---

## Public vs private headers

### Two header categories

| Category | Location | Self-contained? | Stable API? | Installed? |
|----------|----------|-----------------|-------------|------------|
| **Stable** | `include/edge_tts/api/`, `core/`, `common/`, `subtitles/`, `edge_tts.hpp` | Yes | Yes | Yes |
| **Installed, not stable** | `include/edge_tts/communication/`, `serialization/`, `media/`, `cli/` | Yes | No | Yes |

**"Stable"** means the API will not change without a major version bump.  Consumers
linking `edge_tts::tts` and including only stable headers are insulated from
internal refactors.

**"Installed, not stable"** means the header is present in the install tree and
fully self-contained, but it may change or be reorganized in a minor version.
Use these headers only when building a custom transport or extending the library.

### Invariants enforced by automated tests

Every header under `include/edge_tts/` must satisfy all of the following:

| Invariant | Test | CTest name |
|-----------|------|------------|
| Has `#pragma once` | `test_public_headers.py` | `edge_tts_public_header_hygiene_tests` |
| Does not `#include` from `src/`, `tests/`, or absolute paths | `test_public_headers.py` | `edge_tts_public_header_hygiene_tests` |
| Does not `#include` any `Fake*.hpp` test-double | `test_public_headers.py` | `edge_tts_public_header_hygiene_tests` |
| Compiles in isolation at the build tree | `edge_tts_header_selfcontainment_tests` (C++) | built by CMake |
| Compiles in isolation against the install prefix | `test_installed_header_selfcontainment.py` | `edge_tts_installed_header_selfcontainment_tests` |

Stable headers additionally must:

| Invariant | Test | CTest name |
|-----------|------|------------|
| Only introduce `namespace edge_tts::` declarations at file scope | `test_public_headers.py` | `edge_tts_public_header_hygiene_tests` |
| Only include stable API headers from edge-tts-cpp (no cli/, media/, etc.) | `test_umbrella_header_hygiene.py` (for umbrella) | `edge_tts_umbrella_header_hygiene_tests` |

### Per-module header classification

| Module | Public header path | Stable? | Notes |
|--------|-------------------|---------|-------|
| `api` | `include/edge_tts/api/` | Yes | Core synthesis facade |
| `core` | `include/edge_tts/core/` | Yes | Data types (TtsConfig, Voice, Chunk) |
| `common` | `include/edge_tts/common/` | Yes | Error handling, utilities |
| `subtitle` | `include/edge_tts/subtitles/` | Yes | SubMaker, SRT types |
| `communication` | `include/edge_tts/communication/` | No | Internal transport — may change |
| `serialization` | `include/edge_tts/serialization/` | No | Internal protocol framing — may change |
| `media` | `include/edge_tts/media/` | No | App-layer; ffplay/ffmpeg runner |
| `cli` | `include/edge_tts/cli/` | No | App-layer; CLI argument parsing |
| `test_support` | `tests/communication/Fake*.hpp` | — | **Never installed**; test doubles only |

---

## Public consumer targets

### Quick reference

| CMake target | `#include` | What it gives you | Exported? |
|---|---|---|---|
| `edge_tts::tts` | `<edge_tts/edge_tts.hpp>` | **Recommended entry point.** Carries all synthesis deps transitively. | Yes |
| `edge_tts::api` | `<edge_tts/api/Communicate.hpp>` | `Communicate`, `FileWriter`, `CommunicateOptions`. | Yes |
| `edge_tts::core` | `<edge_tts/core/TtsConfig.hpp>` | `TtsConfig`, `Voice`, `TtsChunk`. | Yes |
| `edge_tts::common` | `<edge_tts/common/Result.hpp>` | `Result<T>`, `ErrorCode`, utilities. | Yes |
| `edge_tts::subtitle` | `<edge_tts/subtitles/SubMaker.hpp>` | `SubMaker`, SRT types. | Yes |
| `edge_tts::communication` | `<edge_tts/communication/>` | WebSocket/HTTP transport (advanced). | Yes |
| `edge_tts::serialization` | `<edge_tts/serialization/>` | Protocol framing, SSML (advanced). | Yes |
| `edge_tts::cli` | — | CLI argument parsing. **Not exported.** App-layer only. | No |
| `edge_tts::media` | — | ffmpeg/ffplay runner. **Not exported.** App-layer only. | No |

Consumers should link only `edge_tts::tts` unless they have a specific reason
to depend on a lower-level module.

### Umbrella header

The recommended way to use edge-tts-cpp as a dependency is through the single
umbrella header:

```cpp
#include <edge_tts/edge_tts.hpp>
```

`include/edge_tts/edge_tts.hpp` exposes the complete stable public API:
`Communicate`, `CommunicateOptions`, `FileWriter`, `TtsConfig`, `Voice`,
`Result<T>`, and `ErrorCode`.  It does NOT pull in CLI, media/playback,
internal transport, or test utilities.

Direct includes of individual headers (`edge_tts/api/Communicate.hpp`, etc.)
are also supported for consumers who need finer granularity.

### `edge_tts::tts` (recommended)

**CMake target:** `edge_tts_tts` / `edge_tts::tts`

The stable, minimal entry point for external consumers of the TTS library.

```cmake
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

`edge_tts_tts` is an INTERFACE target that links `edge_tts::api`.  Transitively
provides the full synthesis library:

```
edge_tts::tts
  └─ edge_tts::api
       ├─ edge_tts::communication  (WebSocket, HTTP, DRM token)
       ├─ edge_tts::serialization  (SSML, protocol frames, JSON)
       ├─ edge_tts::subtitle       (SRT generation)
       ├─ edge_tts::core           (TtsConfig, Voice, TtsChunk)
       └─ edge_tts::common         (Error, Result<T>, IClock)
```

`edge_tts::tts` does **not** expose:
- `edge_tts::cli` — CLI argument parsing for the `edge-tts` / `edge-playback` apps
- `edge_tts::media` — ffplay/ffmpeg process runner (not needed for synthesis)
- `edge_tts_test_support` — test-only fake clients

Stability contract: `edge_tts::tts` is stable and consumer-facing.  It will
not be removed or renamed in a patch release.  Breaking changes require a semver
major bump.

### Advanced targets

Lower-level module targets remain available for consumers who need fine-grained
control over link graphs:

| Target | Description |
|--------|-------------|
| `edge_tts::api` | Public synthesis facade only. |
| `edge_tts::communication` | WebSocket/HTTP transport + DRM token. |
| `edge_tts::serialization` | SSML, protocol framing, JSON parsing. |
| `edge_tts::subtitle` | SRT generation. |
| `edge_tts::media` | ffplay/ffmpeg process runner. |
| `edge_tts::core` | Domain types. |
| `edge_tts::common` | Errors, Result, utilities. |

## Broad aggregate target (internal)

`edge_tts` / `edge_tts::edge_tts` / `edge_tts::all` — INTERFACE target linking
all modules including `edge_tts::cli` and `edge_tts::media`.

Used only by internal example programs.  External consumers must use
`edge_tts::tts` instead.  Apps and tests must link specific module targets.

| Alias | Status |
|-------|--------|
| `edge_tts::edge_tts` | Retained for compatibility |
| `edge_tts::all` | Preferred name for the broad aggregate |

---

## Test framework

Tests use a minimal GTest-compatible single-header runner at
`tests/vendor/minigtest/minigtest.hpp`.  No external installation is required.
The supported macro surface is `TEST`, `EXPECT_EQ/NE/TRUE/FALSE`,
`ASSERT_EQ/NE/TRUE/FALSE`, `EXPECT_THROW`, and `EXPECT_NO_THROW`.
