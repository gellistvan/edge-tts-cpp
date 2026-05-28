# Module Ownership Reference

This document describes what each module owns, what it may depend on, and what
it must never import.

---

## `edge_tts::common`

**Headers:** `include/edge_tts/common/`

| File | Description |
|------|-------------|
| `Errors.hpp` | Exception hierarchy rooted at `Error : std::runtime_error`. Concrete types: `ConfigurationError`, `NetworkError`, `ProtocolError`, `AudioError`, `SubtitleError`. |
| `Expected.hpp` | `Expected<T,E>` — variant-based result type for C++20, modelled after `std::expected` (C++23). Use for functions that can fail at runtime without throwing. `Unexpected<E>` is the error-state factory. |
| `Utf8.hpp` | Constexpr UTF-8 byte utilities: `is_continuation(char)`, `safe_boundary(string_view, pos)`, `is_valid(string_view)`. Used by `TextChunker` to avoid splitting mid-sequence. |

**Allowed dependencies:** none (foundational, must stay zero-dep).

---

## `edge_tts::core`

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

**Headers:** `include/edge_tts/serialization/`

Owns the Edge TTS WebSocket protocol format: SSML message construction,
speech-config payloads, and protocol/connection token metadata.

**Allowed dependencies:** `edge_tts::core`, `edge_tts::common`.

**Forbidden:** networking (HTTP/WebSocket I/O).

---

## `edge_tts::subtitles`

**Headers:** `include/edge_tts/subtitles/`

Owns `SubtitleEntry` (start/end/text), `SubMaker` (accumulates boundary events
into subtitle cues), and `SrtComposer` (renders cues to SRT text).

**Allowed dependencies:** `edge_tts::core`, `edge_tts::common`.

---

## `edge_tts::media`

**Headers:** `include/edge_tts/media/`

Owns the `ffmpeg`/`ffplay` process boundary.  Converts or plays audio by
spawning system executables — no direct linking to FFmpeg libraries.

**Allowed dependencies:** `edge_tts::common`.

**Forbidden:** TTS configuration logic, protocol parsing.

---

## `edge_tts::communication`

**Headers:** `include/edge_tts/communication/`

Public facade (`Communicate`), voice-list service (`HttpVoiceService`,
`VoicesManager`), and WebSocket transport abstraction
(`Transport`, `WebSocketTransport`).

**Allowed dependencies:** all other modules.

**Convention:** keep business rules delegated to `core`/`serialization`; this
module should remain a thin orchestration layer.

---

## Test framework

Tests use a minimal GTest-compatible single-header runner at
`tests/vendor/minigtest/minigtest.hpp`.  No external installation is required.
The supported macro surface is `TEST`, `EXPECT_EQ/NE/TRUE/FALSE`,
`ASSERT_EQ/NE/TRUE/FALSE`, `EXPECT_THROW`, and `EXPECT_NO_THROW`.
