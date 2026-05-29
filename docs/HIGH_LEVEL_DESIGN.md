# High-Level Design

`edge-tts-cpp` is organized as a set of small CMake module libraries. Each module owns one namespace and has a clear dependency boundary. The aggregate target `edge_tts::edge_tts` links all modules for application and example users.

## Compatibility baseline

All networking, protocol, DRM, text-processing, and timing decisions must be
derived from `docs/REFERENCE_BEHAVIOR.md`.  That document is the authoritative
inventory of observed Python `edge-tts` v7.2.8 behavior.  Do not infer
protocol details from assumptions — consult `REFERENCE_BEHAVIOR.md` first.

## Module map

| Module target | Source folder | Public include folder | Namespace | Responsibility |
|---|---|---|---|---|
| `edge_tts::common` | `src/common` | `include/edge_tts/common` | `edge_tts::common` | `Exception` hierarchy (`Errors.hpp`), value-type `Error` + `ErrorCode` (`Error.hpp`), `Result<T>` / `Result<void>` (`Result.hpp`), `Expected<T,E>`, and UTF-8 utilities. Must not depend on other modules. |
| `edge_tts::core` | `src/core` | `include/edge_tts/core` | `edge_tts::core` | Domain value types and pure business rules: `TtsConfig`, `Voice`/`VoiceGender`, `Chunk.hpp` types (`BoundaryType`, `AudioChunk`, `BoundaryChunk`, `TtsChunk`), `TextChunker`. No networking, filesystem, process execution, or protocol transport. |
| `edge_tts::serialization` | `src/serialization` | `include/edge_tts/serialization` | `edge_tts::serialization` | Edge protocol serialization and parsing: SSML, speech config payloads, protocol headers, token and connection metadata. May depend on `core` and `common`. |
| `edge_tts::communication` | `src/communication` | `include/edge_tts/communication` | `edge_tts::communication` | Public orchestration and transport-facing services: `Communicate`, voice service, WebSocket transport abstraction and implementation stubs. May depend on all modules but should keep business rules delegated. |
| `edge_tts::media` | `src/media` | `include/edge_tts/media` | `edge_tts::media` | Audio conversion and playback integration. Owns the `ffmpeg`/`ffplay` process boundary. Must not parse protocol messages or own TTS configuration rules. |
| `edge_tts::subtitles` | `src/subtitles` | `include/edge_tts/subtitles` | `edge_tts::subtitles` | Subtitle cue modeling, boundary-to-cue conversion, and SRT composition. May depend on `core` for boundary chunks. |

## Dependency direction

The intended dependency graph is:

```text
common
  ↑
core
  ↑          media
serialization ↑
      subtitles
          ↑
communication
```

Rules:

- `common` is foundational and must remain dependency-free.
- `core` is pure domain logic and must not depend on transport, media, or serialization.
- `serialization` converts core objects to/from Edge protocol payloads.
- `subtitles` converts core boundary chunks into subtitle output.
- `media` owns process execution and audio conversion/playback concerns.
- `communication` coordinates modules and adapts transports; it should remain thin.

## Public API policy

Headers are grouped by module:

```cpp
#include <edge_tts/core/TtsConfig.hpp>
#include <edge_tts/communication/Communicate.hpp>
#include <edge_tts/subtitles/SrtComposer.hpp>
```

Avoid adding new public headers at `include/edge_tts/` root unless they are deliberate umbrella headers. Module-local concepts should stay inside their module folder.

## Core domain type ownership

### `common` module

| Header | Owns |
|--------|------|
| `Errors.hpp` | `Exception` base, `ConfigurationError`, `NetworkError`, `ProtocolError`, `AudioError`, `SubtitleError` — throw-based for programmer errors |
| `Error.hpp` | `ErrorCode` enum, value-type `Error` (code + message + context), `to_string(ErrorCode)` |
| `Result.hpp` | `Result<T>`, `Result<void>`, `BadResultAccess` — return-value error propagation |
| `Expected.hpp` | `Expected<T,E>`, `Unexpected<E>` — generic result type for custom error types |
| `Utf8.hpp` | `utf8::is_continuation` (constexpr), `utf8::safe_boundary` (constexpr, used by TextChunker), `utf8::is_valid_utf8` (full validator), `utf8::previous_code_point_boundary`, `utf8::next_code_point_boundary`, `utf8::split_utf8_by_byte_limit` |

### UTF-8 strategy

`Utf8.hpp` is the single source of truth for UTF-8 boundary and validation logic:

- `split_utf8_by_byte_limit(text, max_bytes)` — use this in `TextChunker` and any future
  byte-limit splitting.  It guarantees that no chunk ends inside a multi-byte sequence.
- `is_valid_utf8(text)` — rejects overlong encodings, UTF-16 surrogates (U+D800–U+DFFF),
  truncated sequences, and code points above U+10FFFF.
- `previous_code_point_boundary` / `next_code_point_boundary` — boundary navigation used by
  the splitter and any protocol code that steps through text.

All protocol-level text handling (XML escaping, SSML generation) must ensure that any
byte-level slice of a string passes `is_valid_utf8` before transmission.

### Error strategy

**When to throw exceptions** — only at API entry points where a wrong argument
is a programmer bug, not a recoverable runtime condition:
- `TtsConfig::validate()` throws `ConfigurationError` for invalid voice/rate/pitch syntax.

**When to return `Result<T>`** — all I/O paths, network operations, protocol
parsing, and service calls where failure is expected and recoverable:
- Networking failures → `Result<T>` with `ErrorCode::network_error`
- Protocol parse errors → `Result<T>` with `ErrorCode::protocol_error`
- File I/O → `Result<T>` with `ErrorCode::io_error`

See `docs/MODULES.md` for the complete Python→C++ `ErrorCode` mapping table.

### `core` module

| Header | Owns |
|--------|------|
| `Chunk.hpp` | `BoundaryEventType` (wire event classifier: `WordBoundary`, `SentenceBoundary`), `AudioChunk` (raw MP3 `vector<byte>`), `BoundaryChunk` (text + offset_ticks + duration_ticks in 100 ns units), `TtsChunk = variant<AudioChunk, BoundaryChunk>`, `is_audio()`, `is_boundary()` predicates |
| `TtsConfig.hpp` | `BoundaryType {word, sentence}` (request config), `TtsConfig` (with `defaults()`, `validate()`, and `OutputFormat`), `validate_tts_config()→Result<void>`, `boundary_type_from_string()`, `to_string(BoundaryType)`, `normalize_voice_name()` |
| `OutputFormat.hpp` | `OutputFormat` — validated audio format string |
| `Voice.hpp` | `VoiceGender`, `Voice` |
| `TextChunker.hpp` | `core::TextChunker` — UTF-8-safe byte splitter only; no normalization, escaping, or entity protection.  Use `serialization::TextChunker` for the full reference pipeline. |

`TtsChunk.hpp` remains as a compatibility shim that re-exports `Chunk.hpp`.

### `serialization` module (text preparation headers)

| Header | Owns |
|--------|------|
| `XmlEscaper.hpp` | `xml_escape` (`&`/`<`/`>` only, matches `xml.sax.saxutils.escape`), `xml_unescape` (reverses `&amp;`/`&lt;`/`&gt;`/`&quot;`/`&apos;`), not idempotent |
| `TextNormalizer.hpp` | `TextNormalizer::normalize()→Result<string>` — UTF-8 validation + control-char replacement (U+0000–U+0008, U+000B–U+000C, U+000E–U+001F → space), CRLF preserved |
| `TextChunker.hpp` | `TextChunkerOptions` (`max_chunk_size`, `size_after_xml_escape`, `prefer_sentence_boundary`, `prefer_word_boundary`) + `TextChunker::chunk()→Result<vector<string>>` — full reference pipeline: normalize → escape → split by escaped byte limit (newline &gt; space &gt; UTF-8 boundary &gt; entity protection), returned chunks are XML-escaped and stripped |
| `SsmlBuilder.hpp` | `SsmlBuilder::build(config, raw_text)→Result<string>` — validates `TtsConfig`, normalizes voice to full form, normalizes and XML-escapes `raw_text` exactly once, returns the complete SSML document body matching Python `mkssml()` (single line, single-quoted attributes, pitch/rate/volume order, `xml:lang='en-US'` hardcoded, no protocol headers) |

**Note:** `Chunk.hpp` uses `BoundaryEventType` (classifying received events) while
`TtsConfig.hpp` uses `BoundaryType` (controlling which events are requested).  These are
distinct concepts despite appearing similar.

### Validation contract

`validate_tts_config(const TtsConfig&)` returns `Result<void>` and accepts voice in
either the short locale form (`en-US-EmmaMultilingualNeural`) or the full
`"Microsoft Server Speech Text to Speech Voice (locale, name)"` form, validating
`rate`, `volume`, and `pitch` against the Edge TTS wire format:

| Field | Pattern |
|-------|---------|
| `rate` | `^[+-]\d+%$` |
| `volume` | `^[+-]\d+%$` |
| `pitch` | `^[+-]\d+Hz$` |

Returns `Result<void>::fail(Error{invalid_argument, ...})` on the first invalid
field, with the field name in the message and the bad value in the context.

`TtsConfig::validate()` is a legacy throw-based bridge over `validate_tts_config()`;
new code should use `validate_tts_config()` directly.  `defaults()` returns a
`TtsConfig` with all fields matching the Python reference defaults.

## Test structure

Tests mirror modules and are built as separate targets.  The test framework is
a minimal GTest-compatible single-header runner located at
`tests/vendor/minigtest/minigtest.hpp` — no external dependency is required.

| Test target | Folder | Linked module |
|---|---|---|
| `edge_tts_common_tests` | `tests/common` | `edge_tts::common` |
| `edge_tts_core_tests` | `tests/core` | `edge_tts::core` |
| `edge_tts_serialization_tests` | `tests/serialization` | `edge_tts::serialization` |
| `edge_tts_communication_tests` | `tests/communication` | `edge_tts::communication` |
| `edge_tts_media_tests` | `tests/media` | `edge_tts::media` |
| `edge_tts_subtitles_tests` | `tests/subtitles` | `edge_tts::subtitles` |

Each module test target should prefer behavior-level tests over implementation-detail tests. Cross-module integration tests may be added later as a separate target once real transport is implemented.

## Current implementation status

| Module | Status |
|--------|--------|
| `common` | `Error.hpp`, `Result.hpp`, `Clock.hpp`, `Hex.hpp`, `IdGenerator.hpp`, `Utf8.hpp`, `Errors.hpp`, `Expected.hpp` implemented |
| `core` | `Chunk.hpp` (`AudioChunk`, `BoundaryChunk`, `TtsChunk`, `is_audio`/`is_boundary`), `Voice.hpp` (all reference fields), `TtsConfig.hpp` (full validation + `validate_tts_config()`), `OutputFormat.hpp`, `TextChunker` (UTF-8 aware) implemented |
| `serialization` | `XmlEscaper.hpp`, `TextNormalizer.hpp`, `TextChunker.hpp`, `SsmlBuilder.hpp` implemented |
| `subtitles` | `SubtitleTime`, `SubtitleCue`, `SrtComposer` implemented |
| `communication` | Skeleton only |
| `media` | Skeleton only |
