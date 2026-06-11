# High-Level Design

`edge-tts-cpp` is organized as a set of small CMake module libraries. Each module owns one namespace and has a clear dependency boundary. The aggregate target `edge_tts::edge_tts` links all modules for application and example users.

## Compatibility baseline

All networking, protocol, DRM, text-processing, and timing decisions must be
derived from `docs/REFERENCE_BEHAVIOR.md`.  That document is the authoritative
behavior specification.  Do not infer protocol details from assumptions —
consult `REFERENCE_BEHAVIOR.md` first.

## Module map

| Module target | Source folder | Public include folder | Namespace | Responsibility |
|---|---|---|---|---|
| `edge_tts::common` | `modules/common/src` | `modules/common/include/common` | `edge_tts::common` | `Exception` hierarchy (`Errors.hpp`), value-type `Error` + `ErrorCode` (`Error.hpp`), `Result<T>` / `Result<void>` (`Result.hpp`), `Expected<T,E>`, and UTF-8 utilities. Must not depend on other modules. |
| `edge_tts::core` | `modules/core/src` | `modules/core/include/core` | `edge_tts::core` | Domain value types and pure business rules: `TtsConfig`, `Voice`/`VoiceGender`, `Chunk.hpp` types (`BoundaryType`, `AudioChunk`, `BoundaryChunk`, `TtsChunk`). No networking, filesystem, process execution, or protocol transport. |
| `edge_tts::serialization` | `modules/serialization/src` | `modules/serialization/include/serialization` | `edge_tts::serialization` | Edge protocol serialization and parsing: SSML, speech config payloads, protocol headers, token and connection metadata. May depend on `core` and `common`. |
| `edge_tts::communication` | `modules/communication/src` | `modules/communication/include/communication` | `edge_tts::communication` | WebSocket transport infrastructure and synthesis orchestration: `IWebSocketClient`, `EdgeProtocol` (frame builders + incoming parser), `SynthesisSession`, voice service, token provider. Depends on `common`, `core`, `serialization` only — no `subtitle` or `media`. Does NOT own the public `SpeechSynthesizer` facade — that belongs in `api`. |
| `edge_tts::api` | `modules/api/src` | `modules/api/include/api` | `edge_tts::api` | **Public synthesis facade.** `SpeechSynthesizer` — the only class end-users and the CLI layer should import. Orchestrates `SynthesisSession`, `serialization::TextChunker`, `SubtitleBuilder`, and media I/O. `FileWriter` handles binary media and UTF-8 text file writes for `save()`. Sits above `communication` so the transport layer stays clean. |
| `edge_tts::media` | `modules/media/src` | `modules/media/include/media` | `edge_tts::media` | Audio conversion and playback integration. Owns the `ffmpeg`/`ffplay` process boundary. Must not parse protocol messages or own TTS configuration rules. **Platform note:** `ProcessRunner` (POSIX fork/exec) is excluded from the Windows build by CMake; `FakeProcessRunner` (test-support only, `tests/support/`) and the `IAudioConverter` interface compile everywhere. `media` is not part of the core synthesis pipeline — TTS, networking, and voice listing work without it. |
| `edge_tts::subtitles` | `modules/subtitle/src` | `modules/subtitle/include/subtitles` | `edge_tts::subtitles` | Subtitle cue modeling, boundary-to-cue conversion, and SRT composition. May depend on `core` for boundary chunks. |

## Dependency direction

The intended dependency graph is:

```text
common
  ↑
core ─────────────────────────────┐
  ↑                               ↑
serialization   media   subtitle  │
      ↑           ↑        ↑      │
      └─── communication   │      │
                    ↑      │      │
                   api ────┴──────┘
                    ↑
                   cli
```

Rules:

- `common` is foundational and must remain dependency-free.
- `core` is pure domain logic and must not depend on transport, media, or serialization.
- `serialization` converts core objects to/from Edge protocol payloads.
- `subtitles` converts core boundary chunks into subtitle output.
- `media` owns process execution and audio conversion/playback concerns.
- `communication` depends on `common`, `core`, and `serialization` only.  It is pure
  transport orchestration — WebSocket/HTTP framing, session lifecycle, DRM token.  It
  must not include `subtitle` or `media`; those concerns belong in `api`.
  - `IWebSocketClient` is the WebSocket transport boundary.
  - `EdgeProtocol` owns all outgoing frame construction and incoming frame dispatch.
  - `SynthesisSession` orchestrates the full per-chunk synthesis lifecycle:
    URL construction → connect → speech.config → SSML → receive loop → close.
    One `IWebSocketClient` connection is opened and closed per text chunk.
    `SynthesisSession` also owns **boundary offset compensation**: it tracks
    cumulative audio bytes across chunks and adds
    `cumulative_bytes * 8 * 10_000_000 / 48_000` ticks to each
    `BoundaryChunk::offset_ticks` so subtitles align correctly across chunk
    boundaries.  `duration_ticks` is never modified.

**TextChunker / SsmlBuilder escaping contract:**

`serialization::TextChunker::chunk()` returns XML-escaped strings — escaping happens
once at the chunking step, sized against the escaped byte length (4096-byte limit).

`serialization::SsmlBuilder` provides two entry points:
- `build(config, raw_text)` — for raw user text: normalizes + XML-escapes + assembles SSML.
- `build_from_escaped_text(config, escaped_text)` — for pre-escaped text: assembles SSML
  without any additional escaping.  This is what `EdgeProtocol::build_ssml_frame` uses.

`EdgeProtocol::build_ssml_frame` expects pre-escaped input (from `TextChunker`) and
calls `build_from_escaped_text` to embed it verbatim.  This ensures XML-escaping
occurs **exactly once** across the full pipeline and never produces `&amp;amp;`.

## Public API policy

Headers are grouped by module:

```cpp
#include "edge_tts/api/SpeechSynthesizer.hpp"        // public TTS facade
#include "edge_tts/api/SynthesisOptions.hpp" // transport / proxy options
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/subtitles/SrtComposer.hpp"
```

Avoid adding new public headers at `include/edge_tts/` root unless they are deliberate umbrella headers. Module-local concepts should stay inside their module folder.

### Speech config vs. transport options

`core::TtsConfig` is **speech-only**: voice, rate, volume, pitch.  It must
never hold transport settings (proxy URL, timeouts).

`api::SynthesisOptions` is **transport-only**: proxy URL, WebSocket connect/
read timeouts, HTTP timeout.  It must never hold speech settings.

The separation keeps `TtsConfig` serializable into SSML without any network
knowledge, and lets transport configuration evolve independently.

| What | Type | Field |
|------|------|-------|
| Voice, rate, volume, pitch | `core::TtsConfig` | `voice`, `rate`, `volume`, `pitch` |
| HTTP/WebSocket proxy | `api::SynthesisOptions` | `proxy` (**parsed and validated; currently rejected at runtime** — ixwebsocket has no client-side proxy API; returns `unsupported`) |
| WS connect timeout | `api::SynthesisOptions` | `ws_connect_timeout` (default 10 s) |
| WS read timeout | `api::SynthesisOptions` | `ws_read_timeout` (default 60 s) |
| HTTP timeout | `api::SynthesisOptions` | `http_timeout` (default 30 s) |

### `SpeechSynthesizer` constructor matrix

| Constructor | Purpose |
|-------------|---------|
| `SpeechSynthesizer(text, config = {})` | Production; default options; real networking stack |
| `SpeechSynthesizer(text, config, SynthesisOptions)` | Production with explicit proxy/timeouts |
| `SpeechSynthesizer(text, config, SynthesizerFn)` | Test injection; default options |
| `SpeechSynthesizer(text, config, SynthesisOptions, SynthesizerFn)` | Test injection with options (seam test) |

The two production constructors own a heap-allocated `ProductionSynthesizer`
that composes the full networking stack at construction time:
`SystemClock → IdGenerator → EdgeServiceConfig → EdgeTokenProvider →
EdgeProtocol → ConnectionMetadataFactory → WebSocketClient → SynthesisSession`.
No network work is performed in the constructor — synthesis is deferred to
`synthesize()` / `save()` call time.

## C++ usage example

```cpp
#include "edge_tts/api/SpeechSynthesizer.hpp"
#include "edge_tts/api/SynthesisOptions.hpp"
#include "edge_tts/core/TtsConfig.hpp"

// Build speech configuration.
edge_tts::core::TtsConfig cfg;
cfg.voice = "en-US-EmmaMultilingualNeural";
cfg.rate  = "+0%";

// Build transport options (optional — all fields have sensible defaults).
edge_tts::api::SynthesisOptions opts;
// opts.proxy = "http://proxy.example.com:8080";
// NOTE: the ixwebsocket backend does not support client-side proxy.
// Setting proxy returns ErrorCode::unsupported from synthesize()/save().
opts.ws_connect_timeout = std::chrono::milliseconds{10'000};
opts.ws_read_timeout    = std::chrono::milliseconds{60'000};

// Synthesize text — speech config and transport options are separate.
edge_tts::api::SpeechSynthesizer c("Hello, world!", std::move(cfg), std::move(opts));

// Save audio and optional SRT subtitles.
auto result = c.save("hello.mp3", "hello.srt");
if (!result) {
    std::cerr << result.error().what() << '\n';
}

// OR batch-collect all chunks.
edge_tts::api::SpeechSynthesizer c2("Hello again!");
auto chunks = c2.synthesize();
if (chunks) {
    for (const auto& chunk : *chunks) {
        if (edge_tts::core::is_audio(chunk)) { /* write audio bytes */ }
        else                                  { /* process boundary event */ }
    }
}

// OR use the progressive callback API (streaming).
#include "edge_tts/api/StreamCallbacks.hpp"

edge_tts::api::SpeechSynthesizer c3("Streaming example!");
edge_tts::api::StreamCallbacks cbs;
cbs.on_audio = [](std::span<const std::byte> data) {
    // Write data to a file, pipe, or audio device as it arrives.
};
cbs.on_boundary = [](const edge_tts::core::BoundaryChunk& b) {
    // Accumulate subtitle cues in arrival order.
};
cbs.on_complete = []() { /* synthesis finished */ };
cbs.on_error    = [](const edge_tts::common::Error& e) {
    std::cerr << e.what() << '\n';
};
auto result = c3.synthesize_stream(std::move(cbs));
```

**Note:** `synthesize()`, `save()`, and `synthesize_stream()` are all single-use.
Calling any of them a second time returns `ErrorCode::invalid_state`. Inject a
`SynthesizerFn` for unit testing without a live service connection.

### `synthesize_stream()` — progressive callback API

`SpeechSynthesizer::synthesize_stream(StreamCallbacks)` delivers chunks one at a
time via callbacks instead of accumulating them in a vector.  This lets callers
write a file writer, audio player, or subtitle builder without holding all audio
bytes in memory simultaneously.

| Callback | When called | Optional? |
|----------|-------------|-----------|
| `on_audio(span<const byte>)` | Each audio chunk, in order | yes |
| `on_boundary(const BoundaryChunk&)` | Each word/sentence boundary, in order | yes |
| `on_complete()` | Once, on success | yes |
| `on_error(const Error&)` | Once, on failure or cancellation | yes |

Exactly one of `on_complete` / `on_error` fires last.  All callbacks are invoked
synchronously on the calling thread.  Null `std::function` members are silently
skipped.  The `span` passed to `on_audio` is valid only for the duration of that
call — copy the bytes if they must outlive it.

**Cancellation:** call `SpeechSynthesizer::cancel()` at any time, including from
inside a callback.  Dispatch stops before the next chunk; `on_error` fires with
`ErrorCode::cancelled`.

## Core domain type ownership

### `common` module

| Header | Owns |
|--------|------|
| `Errors.hpp` | `Exception` base, `ConfigurationError`, `NetworkError`, `ProtocolError`, `AudioError`, `SubtitleError` — throw-based for programmer errors |
| `Error.hpp` | `ErrorCode` enum, value-type `Error` (code + message + context), `to_string(ErrorCode)` |
| `Result.hpp` | `Result<T>`, `Result<void>`, `BadResultAccess` — return-value error propagation |
| `Expected.hpp` | `Expected<T,E>`, `Unexpected<E>` — generic result type for custom error types |
| `Utf8.hpp` | `utf8::is_continuation` (constexpr), `utf8::safe_boundary` (constexpr, used by `serialization::TextChunker`), `utf8::is_valid_utf8` (full validator), `utf8::previous_code_point_boundary`, `utf8::next_code_point_boundary`, `utf8::split_utf8_by_byte_limit` |

### UTF-8 strategy

`Utf8.hpp` is the single source of truth for UTF-8 boundary and validation logic:

- `split_utf8_by_byte_limit(text, max_bytes)` — use this in `serialization::TextChunker` and any future
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
- File I/O → `Result<T>` with `ErrorCode::io_error` (context carries the file path)
- DRM token rejection → `Result<T>` with `ErrorCode::drm_error` (context carries the server `Date` header for clock-skew correction)
- Service-level refusal → `Result<T>` with `ErrorCode::service_error` (e.g. no audio received, HTTP 4xx/5xx that is not 403)

**Error context fields** — the `Error::context()` string carries machine-readable diagnostic data:

| Error code | Context content | Example |
|------------|-----------------|---------|
| `io_error` | Absolute file path | `/tmp/output.mp3` |
| `drm_error` | Server `Date` HTTP header | `Thu, 01 Jan 1970 00:00:30 GMT` |
| `service_error` | HTTP status code (as string) | `"403"`, `"503"` |
| `network_error` | WebSocket URL or close reason | `wss://speech.platform.bing.com/...` |
| `unsupported` | Proxy URL with credentials replaced by `[credentials]` | `"http://[credentials]@host:8080"` |

**Proxy credential redaction** — credentials (`user:pass@`) are sanitized at the
source: `WebSocketClient::connect()` and `HttpClient::send()` call an inline
`sanitize_proxy_url()` helper before storing the URL in `Error::context()`.
Both CLI dispatchers (`EdgeTtsCommandDispatcher`, `PlaybackCommandDispatcher`)
additionally apply `redact_url_credentials()` when formatting errors for stderr,
providing defense-in-depth.  Any `error.context()` that contains a proxy URL
will never expose raw credentials in any code path.

**HTTP transport vs. service layer** — `IHttpClient::send()` returns a
successful `Result<HttpResponse>` for any HTTP status code, including non-2xx.
Transport-level failures (network unreachable, TLS error, timeout, unsupported
proxy) are `Result::fail`.  Service-level interpretation — mapping HTTP 403 to a
DRM retry or HTTP 5xx to `service_error` — is the responsibility of the caller
(`VoiceService`), not the transport.

**No exceptions across the public API** — `synthesize()`, `save()`, and all `VoiceService` methods return `Result<T>` for all recoverable failures. The only exceptions are `BadResultAccess` (programmer error: accessing a failed `Result<T>`) and `ConfigurationError` (programmer error: invalid `TtsConfig`).

See `docs/MODULES.md` for the complete `ErrorCode` reference.

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
| `XmlEscaper.hpp` | `xml_escape` (`&`/`<`/`>` only; `"` and `'` are unchanged), `xml_unescape` (reverses `&amp;`/`&lt;`/`&gt;`/`&quot;`/`&apos;`), not idempotent |
| `TextNormalizer.hpp` | `TextNormalizer::normalize()→Result<string>` — UTF-8 validation + control-char replacement (U+0000–U+0008, U+000B–U+000C, U+000E–U+001F → space), CRLF preserved |
| `TextChunker.hpp` | `TextChunkerOptions` (`max_chunk_size`, `size_after_xml_escape`, `prefer_sentence_boundary`, `prefer_word_boundary`) + `TextChunker::chunk()→Result<vector<string>>` — full reference pipeline: normalize → escape → split by escaped byte limit (newline &gt; space &gt; UTF-8 boundary &gt; entity protection), returned chunks are XML-escaped and stripped |
| `SsmlBuilder.hpp` | `SsmlBuilder::build(config, raw_text)→Result<string>` — validates `TtsConfig`, normalizes voice to full form, normalizes and XML-escapes `raw_text` exactly once, returns the complete SSML document body (single line, single-quoted attributes, pitch/rate/volume order, `xml:lang='en-US'` hardcoded, no protocol headers) |

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
`TtsConfig` with all fields at their default values.

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
| `edge_tts_api_tests` | `tests/api` | `edge_tts::api` |
| `edge_tts_media_tests` | `tests/media` | `edge_tts::media` |
| `edge_tts_subtitles_tests` | `tests/subtitles` | `edge_tts::subtitles` |

Each module test target should prefer behavior-level tests over implementation-detail tests.

### Offline integration coverage

`edge_tts_api_tests` contains cross-module offline integration tests that exercise the complete path from user text to audio chunks and subtitles without any real network access.  The three source files in `tests/api/` form a three-layer coverage strategy:

```
CommunicateEndToEndTests.cpp        — happy-path: MP3, SRT, XML escape, UTF-8, long text
CommunicateProductionWiringTests.cpp — construction wiring: lazy init, no placeholder
OfflineIntegrationTests.cpp         — protocol layer: frame structure, error propagation
```

`OfflineIntegrationTests.cpp` exercises the full path `Communicate → SynthesisSession → EdgeProtocol → FakeWebSocketClient → FileWriter` and proves:

1. **Frame structure**: the client sends `speech.config` (with `Path:speech.config` and `Content-Type:application/json`) then `ssml` (with `Path:ssml` and a 32-char hex `X-RequestId`) for every chunk.
2. **Escaping correctness**: "Tom & Jerry `<test>`" arrives in the SSML frame as `&amp;` / `&lt;` / `&gt;` — never double-escaped, never raw.
3. **Multi-chunk offset compensation**: a 5000-byte input splits into two chunks; boundaries from chunk 2 have their `offset_ticks` shifted by the cumulative `max(offset_ticks + duration_ticks)` from chunk 1, verified both via `synthesize()` chunk values and via the SRT timestamp in `save()` output.
4. **Error propagation**: an unknown `Path` header returns `protocol_error`; a transport drop injected via `set_receive_error` returns `network_error`; `turn.end` with no preceding audio returns `service_error` with a message mentioning "audio".

## Current implementation status

| Module | Status |
|--------|--------|
| `common` | `Error.hpp`, `Result.hpp`, `Clock.hpp`, `Hex.hpp`, `IdGenerator.hpp`, `Utf8.hpp`, `Errors.hpp`, `Expected.hpp` implemented |
| `core` | `Chunk.hpp` (`AudioChunk`, `BoundaryChunk`, `TtsChunk`, `is_audio`/`is_boundary`), `Voice.hpp` (all reference fields), `TtsConfig.hpp` (full validation + `validate_tts_config()`), `OutputFormat.hpp` implemented |
| `serialization` | `XmlEscaper.hpp`, `TextNormalizer.hpp`, `TextChunker.hpp` (Edge SSML chunker: normalize → XML-escape → split at 4096-byte escaped limit), `SsmlBuilder.hpp` implemented |
| `subtitles` | `SubtitleTime`, `SubtitleCue`, `SrtComposer`, `SubtitleBuilder` implemented |
| `communication` | `EdgeServiceConfig`, `EdgeTokenProvider`, `ConnectionMetadataFactory`, `EdgeRequestHeaders`, `EdgeProtocol` (frame builder + parser), `RetryPolicy`, `IHttpClient` / `HttpClient` (ixwebsocket impl, Pimpl), `IWebSocketClient` / `WebSocketClient` (ixwebsocket impl, Pimpl), `VoiceService`, `SynthesisSession` (per-chunk WebSocket lifecycle, 403 retry) — fully implemented. Test doubles (`FakeHttpClient`, `FakeWebSocketClient`) live in `tests/support/`. |
| `api` | `SpeechSynthesizer` facade implemented: validate → chunk → synthesize (via `SynthesizerFn`) → stream/save; `FileWriter` (binary + UTF-8 text writes) implemented |
| `media` | `ProcessRunner` (fork/execvp/waitpid, POSIX-only), `FfmpegAudioConverter` (ffmpeg/ffplay via `IProcessRunner`), `ExecutableDiscovery`, `IAudioConverter` — fully implemented |

## Error handling reference

All recoverable failures surface as `common::Result<T>` with a `common::Error` carrying:
- `error.code()` — a `common::ErrorCode` enum value
- `error.message()` — a human-readable English string
- `error.context()` — optional machine-readable diagnostic data (file path, HTTP status, etc.)
- `error.what()` — formatted `"[code] message (context: ...)"` for logging

### ErrorCode reference

| Code | When it fires | Retryable? | Context contract | Consumer action |
|------|---------------|------------|------------------|-----------------|
| `invalid_argument` | Bad caller input: empty voice, malformed rate/pitch, negative ticks | No | Offending value or parameter name | Fix the argument before retrying |
| `invalid_state` | `synthesize()` or `save()` called a second time on a single-use `SpeechSynthesizer` | No | — | Construct a new `SpeechSynthesizer` |
| `io_error` | Local file open or write failure | Maybe (disk full → no; wrong path → fix path) | Absolute file path | Log the path from `context()`; check disk space and permissions |
| `network_error` | WebSocket or HTTP transport failure (connect refused, TLS error, connection reset) | Yes (with backoff) | Optional; never contains the DRM token or raw credentials | Retry; surface the message to the user |
| `protocol_error` | Unexpected wire message structure (unknown `Path:` header, missing `X-RequestId`, malformed frame) | No (indicates service change or library bug) | The offending wire value | Report with `what()` for diagnostics |
| `parse_error` | JSON decode failure (voice list, metadata) or HTTP-like frame parsing failure | No | — | Report with `what()`; may indicate a service format change |
| `timeout` | WebSocket receive or HTTP request exceeded `SynthesisOptions::ws_read_timeout` / `http_timeout` | Yes | — | Retry; consider increasing the timeout via `SynthesisOptions` |
| `unsupported` | Proxy set but not implemented; platform-level stub (e.g. `WebSocketClient` without ixwebsocket) | No | Proxy URL with credentials replaced by `[credentials]` | Remove `--proxy` or disable the unsupported feature |
| `external_process_failed` | `ffplay` / `ffmpeg` not found on PATH, or subprocess exited non-zero | Maybe (install deps) | Process stderr (may be empty) | Check that ffplay is installed and on PATH |
| `service_error` | Non-200 (non-403) HTTP response; or no audio received after `turn.end` | Yes (for 5xx) | HTTP status code string, e.g. `"429"`, `"503"` | For 5xx: retry with backoff. For 4xx: check request parameters. |
| `drm_error` | HTTP 403 during WebSocket upgrade (DRM token rejected) | Yes — handled internally | Server `Date` header (RFC 2616) for clock-skew correction | The library retries once automatically via `RetryPolicy`. Persistent `drm_error` after retry indicates a clock-skew problem that `EdgeTokenProvider::adjust_clock_skew()` cannot resolve. |
| `cancelled` | `SpeechSynthesizer::cancel()` was called before or during synthesis | No (caller-initiated) | — | Expected — the caller requested cancellation |
| `none` | No error (should not appear in a failed `Result<T>`) | N/A | N/A | Treat as an internal error if seen in a failed result |

### Context field policy

The context field carries **safe, machine-readable diagnostics** — never raw secrets:

- **File paths** (`io_error`) — the absolute path of the file that failed to open or write.
- **HTTP status codes** (`service_error`) — the numeric status as a string (`"429"`, `"503"`).
- **Server Date header** (`drm_error`) — the RFC 2616 `Date` value from the 403 response, used for clock-skew correction.
- **Proxy URLs** (`unsupported`) — redacted: `user:pass@host` → `[credentials]@host` at the call site before being stored. Consumers may log `context()` safely.
- **Wire values** (`protocol_error`) — the offending `Path:` header or frame field, never a full payload.
- **Absent by design** (`cancelled`, `timeout`, `network_error`) — no context needed; the message is sufficient.

**DRM token security:** The DRM token (`Sec-MS-GEC`) is a computed hash and never stored or returned in any error field.

### Consumer error handling pattern

```cpp
auto result = tts.save("hello.mp3");
if (!result) {
    const auto& err = result.error();
    switch (err.code()) {
    case edge_tts::common::ErrorCode::network_error:
    case edge_tts::common::ErrorCode::timeout:
        // Transient — retry with backoff
        retry_later();
        break;
    case edge_tts::common::ErrorCode::invalid_argument:
    case edge_tts::common::ErrorCode::invalid_state:
        // Programmer error — fix the call site
        std::cerr << "BUG: " << err.what() << '\n';
        break;
    case edge_tts::common::ErrorCode::cancelled:
        // Expected — user cancelled
        break;
    default:
        // Log and surface to the user
        std::cerr << "error: " << err.what() << '\n';
    }
}
```

### Exception vs. Result<T>

Only two call sites in the library throw exceptions — both indicate programmer errors, not runtime conditions:

| Throw site | Exception type | Trigger |
|------------|----------------|---------|
| `TtsConfig::validate()` | `common::ConfigurationError` | Invalid voice/rate/pitch syntax — caught at the CLI layer before reaching synthesis |
| `Result<T>::value()` on a failed result | `common::BadResultAccess` | Calling `value()` without checking success — a programming mistake |

All other failure modes, including every network, service, I/O, and protocol failure, propagate via `Result<T>` and never throw.

## Platform support

| Component | Linux | macOS | Windows | Notes |
|-----------|-------|-------|---------|-------|
| Core library (`common`, `core`, `serialization`, `communication`, `api`, `subtitles`) | ✓ | ✓ | ✓ | Pure C++20; no POSIX-only APIs |
| `media` — `ExecutableDiscovery`, `FfmpegAudioConverter`, `IAudioConverter` | ✓ | ✓ | ✓ | Cross-platform via `std::filesystem`; `IProcessRunner` interface decouples from OS |
| `media` — `ProcessRunner` | ✓ | ✓ | ✗ | Uses `fork`/`execvp`/`waitpid`; excluded from Windows build (`if(NOT WIN32)` in CMake, `#ifndef _WIN32` in header). Implement `IProcessRunner` with `CreateProcess` to enable playback on Windows. |
| `edge-tts` CLI app | ✓ | ✓ | ✓ | No POSIX dependencies; builds cleanly on Windows with `-DEDGE_TTS_BUILD_APPS=ON` |
| `edge-playback` CLI app | ✓ | ✓ | ✗ | Requires `ProcessRunner`. `EDGE_TTS_BUILD_PLAYBACK_APP` defaults `OFF` on Windows. Setting it `ON` is a configure-time `FATAL_ERROR` that names the platform and suggests the fix. |
| TLS (`USE_TLS=ON`, default) | OpenSSL | OpenSSL | mbedtls | ixwebsocket selects the backend per platform. Linux/macOS require system OpenSSL. Windows uses ixwebsocket's bundled mbedtls (no extra install needed). |
| `FakeProcessRunner` (test support only) | ✓ | ✓ | ✓ | In-memory test double; no POSIX APIs; always cross-platform |

### Library-only Windows build

All modules except `ProcessRunner` compile on Windows. The recommended CMake invocation for a Windows library-only build:

```cmake
cmake -S . -B build \
  -DEDGE_TTS_BUILD_APPS=ON \
  -DEDGE_TTS_BUILD_PLAYBACK_APP=OFF \
  -DEDGE_TTS_REQUIRE_NETWORKING=ON
```

`EDGE_TTS_BUILD_PLAYBACK_APP` is silently coerced to `OFF` on Windows even when not set explicitly. Setting it `ON` emits a `FATAL_ERROR` that names the cause and the fix:

```
EDGE_TTS_BUILD_PLAYBACK_APP=ON is not supported on Windows.
ProcessRunner requires POSIX APIs (fork/execvp/pipe/waitpid) which
are not available on Windows (platform: Windows).
Options:
  - Set EDGE_TTS_BUILD_PLAYBACK_APP=OFF to build the core library and
    edge-tts CLI without playback.
  - Provide a Windows-specific IProcessRunner implementation and set
    EDGE_TTS_BUILD_PLAYBACK_APP=ON once it compiles.
```

To add Windows playback support, implement `IProcessRunner` using `CreateProcess` / `ReadFile` / `WaitForSingleObject` and link it to `edge_tts::media`.
