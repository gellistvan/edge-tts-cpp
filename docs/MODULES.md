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
| `Expected.hpp` | Generic `Expected<T,E>` / `Unexpected<E>` for cases that need a custom error type. |
| `Utf8.hpp` | Constexpr UTF-8 byte utilities: `is_continuation(char)`, `safe_boundary(string_view, pos)`, `is_valid(string_view)`. Used by `TextChunker` to avoid splitting mid-sequence. |

**Allowed dependencies:** none (foundational, must stay zero-dep).

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
| `TextChunker.hpp` | `TextChunker(max_bytes)` — splits text into byte-capped chunks at UTF-8 code-point boundaries. |

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
speech-config payloads, and protocol/connection token metadata.

**Allowed dependencies:** `edge_tts::core`, `edge_tts::common`.

**Forbidden:** networking (HTTP/WebSocket I/O).

---

## `edge_tts::subtitle`

**CMake target:** `edge_tts_subtitle` / `edge_tts::subtitle`
**Test target:** `edge_tts_subtitle_tests`
**Headers:** `include/edge_tts/subtitles/`
**Sources:** `src/subtitles/`

Owns `SubtitleEntry` (start/end/text), `SubMaker` (accumulates boundary events
into subtitle cues), and `SrtComposer` (renders cues to SRT text).

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

---

## `edge_tts::communication`

**CMake target:** `edge_tts_communication` / `edge_tts::communication`
**Test target:** `edge_tts_communication_tests`
**Headers:** `include/edge_tts/communication/`

Public facade (`Communicate`), voice-list service (`HttpVoiceService`,
`VoicesManager`), and WebSocket transport abstraction
(`Transport`, `WebSocketTransport`).

**Allowed dependencies:** all modules below it in the dependency graph.

**Convention:** keep business rules delegated to `core`/`serialization`; this
module should remain a thin orchestration layer.

---

## `edge_tts::cli`

**CMake target:** `edge_tts_cli` / `edge_tts::cli`
**Test target:** `edge_tts_cli_tests`
**Headers:** `include/edge_tts/cli/`
**Sources:** `src/cli/`

CLI argument parsing and application-level plumbing shared between the
`edge-tts` and `edge-playback` executables.  Will use CLI11 for argument
parsing once the dependency is wired.

**Allowed dependencies:** `edge_tts::communication` only.

**Forbidden:** direct access to `core`, `serialization`, or `media` internals —
route through `communication`.

---

## Aggregate target

`edge_tts` / `edge_tts::edge_tts` — INTERFACE target linking all modules.
Used only by example programs.  Applications and tests must link specific
module targets.

---

## Test framework

Tests use a minimal GTest-compatible single-header runner at
`tests/vendor/minigtest/minigtest.hpp`.  No external installation is required.
The supported macro surface is `TEST`, `EXPECT_EQ/NE/TRUE/FALSE`,
`ASSERT_EQ/NE/TRUE/FALSE`, `EXPECT_THROW`, and `EXPECT_NO_THROW`.
