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
| `edge_tts::common` | header-only | `include/edge_tts/common` | `edge_tts::common` | Shared errors (`Errors.hpp`), `Expected<T,E>`, and UTF-8 byte utilities (`Utf8.hpp`). Must not depend on other modules. |
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
| `Errors.hpp` | `Error`, `ConfigurationError`, `NetworkError`, `ProtocolError`, `AudioError`, `SubtitleError` |
| `Expected.hpp` | `Expected<T,E>`, `Unexpected<E>` — lightweight result type for C++20 |
| `Utf8.hpp` | `utf8::is_continuation`, `utf8::safe_boundary`, `utf8::is_valid` |

### `core` module

| Header | Owns |
|--------|------|
| `Chunk.hpp` | `BoundaryType`, `AudioChunk`, `BoundaryChunk`, `TtsChunk` (variant) |
| `TtsConfig.hpp` | `TtsConfig`, `normalize_voice_name()` |
| `Voice.hpp` | `VoiceGender`, `Voice` |
| `TextChunker.hpp` | `TextChunker` |

`TtsChunk.hpp` remains as a compatibility shim that re-exports `Chunk.hpp`.

### Validation contract

`TtsConfig::validate()` normalizes the `voice` field from the short locale form
(`en-US-EmmaMultilingualNeural`) to the full
`"Microsoft Server Speech Text to Speech Voice (locale, name)"` form and validates
`rate`, `volume`, and `pitch` against the Edge TTS wire format:

| Field | Pattern |
|-------|---------|
| `rate` | `^[+-]\d+%$` |
| `volume` | `^[+-]\d+%$` |
| `pitch` | `^[+-]\d+Hz$` |

Throws `common::ConfigurationError` on any violation.  Safe to call multiple
times (idempotent after the first call).

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
| `common` | `Errors.hpp`, `Expected.hpp`, `Utf8.hpp` implemented |
| `core` | `Chunk.hpp`, `Voice.hpp`, `TtsConfig.hpp` (full validation), `TextChunker` (UTF-8 aware) implemented |
| `serialization` | Skeleton only |
| `subtitles` | `SrtComposer` implemented |
| `communication` | Skeleton only |
| `media` | Skeleton only |
