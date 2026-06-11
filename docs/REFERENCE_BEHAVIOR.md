# Behavior Specification

This document is the authoritative behavior specification for `edge-tts-cpp`.
Every future implementation task that touches networking, protocol handling,
text processing, or audio timing **must consult this document** before making
design decisions.  If a behavior described below is ambiguous or requires live
service verification, that is noted explicitly.

---

# CLI Commands

Two CLI commands are registered:

| Command | Binary |
|---------|--------|
| `edge-tts` | Main TTS synthesizer |
| `edge-playback` | Playback wrapper (calls `edge-tts` + mpv) |

`edge-playback` delegates all TTS work to `edge-tts` via subprocess.  It accepts
all `edge-tts` options except `--write-media`, `--write-subtitles`, and
`--list-voices`.

---

# CLI Options

| Flag | Short | Default | Description |
|------|-------|---------|-------------|
| `--text` | `-t` | (required) | Text to synthesize |
| `--file` | `-f` | — | Read text from file; `-` or `/dev/stdin` reads stdin |
| `--list-voices` | `-l` | — | List voices and exit (mutually exclusive with `--text`/`--file`) |
| `--voice` | `-v` | `en-US-EmmaMultilingualNeural` | TTS voice |
| `--rate` | — | `+0%` | Speech rate |
| `--volume` | — | `+0%` | Speech volume |
| `--pitch` | — | `+0Hz` | Speech pitch |
| `--write-media` | — | stdout | Output file for MP3 audio; `-` → stdout |
| `--write-subtitles` | — | stderr | Output file for SRT; `-` → stderr |
| `--proxy` | — | none | HTTP proxy for TTS and voice list |
| `--version` | — | — | Print version and exit |

`--text` and `--file` and `--list-voices` form a mutually exclusive required group.

**Interactive safety warning:** if both stdin and stdout are TTYs and no
`--write-media` is given, the CLI prints a warning to stderr and waits for
Enter before proceeding.

---

# Default Values

| Parameter | Default |
|-----------|---------|
| `voice` | `en-US-EmmaMultilingualNeural` |
| `rate` | `+0%` |
| `volume` | `+0%` |
| `pitch` | `+0Hz` |
| `boundary` | `SentenceBoundary` |
| `output_format` | `audio-24khz-48kbitrate-mono-mp3` |
| `connect_timeout` | `10` seconds |
| `receive_timeout` | `60` seconds |

`TtsConfig::defaults()` returns a struct with all of these values.

### Validation patterns

| Field | Regex | Accepted examples | Rejected examples |
|-------|-------|-------------------|-------------------|
| `rate` | `^[+-]\d+%$` | `+0%`, `-50%`, `+100%` | `fast`, `++10%`, `10`, `+10`, `+10percent` |
| `volume` | `^[+-]\d+%$` | `+0%`, `-50%`, `+100%` | `loud`, `10%`, `+10percent` |
| `pitch` | `^[+-]\d+Hz$` | `+0Hz`, `-50Hz`, `+100Hz` | `high`, `+10%`, `10Hz`, `+10` |
| `voice` | short or full form | `en-US-EmmaMultilingualNeural`, full "Microsoft Server Speech..." | empty, `not-a-voice` |

### Boundary type wire strings

| C++ enum | Wire value |
|----------|------------|
| `BoundaryType::sentence` | `sentenceBoundaryEnabled: "true"` |
| `BoundaryType::word` | `wordBoundaryEnabled: "true"` |

---

# Edge Service Constants

**C++ implementation:** `communication::EdgeServiceConfig` struct +
`default_edge_service_config()` factory in
`src/communication/EdgeServiceConfig.cpp`.

All Edge TTS hard-coded constants are centralized here. Networking and
serialization code must receive them via `EdgeServiceConfig`, not inline them.

See `docs/PROTOCOL_NOTES.md` — Service Constants section — for the full value
table.

---

# Protocol Text Frame Parsing and Serialization

**C++ implementation:** `serialization::ProtocolMessage`, `serialization::ProtocolParser`,
`serialization::ProtocolSerializer`.

**Wire format:** `Name:Value\r\n` per header, `\r\n\r\n` separator, body verbatim.
No space between header name and colon; no space between colon and value.

**Body offset:** The C++ parser uses `find("\r\n\r\n") + 4` to skip the full separator,
giving a clean body start with no leading whitespace.

**Header ordering:** Preserved in wire order. Duplicate header names are kept as
separate entries.

**LF-only frames:** Rejected — parser splits exclusively on `\r\n`.

---

# Metadata JSON Parsing

**C++ implementation:** `serialization::MetadataJsonParser`

**JSON root:** Object with `"Metadata"` array.

**Handled types:** `"WordBoundary"` → `BoundaryEventType::WordBoundary`;
`"SentenceBoundary"` → `BoundaryEventType::SentenceBoundary`.

**Skipped types:** `"SessionEnd"`.

**Unknown types:** `parse_error`.

**XML unescape:** Applied to `Data.text.Text` field before storing in `BoundaryChunk::text`.

**Offset compensation:** NOT applied in the parser. `MetadataJsonParser` returns
raw ticks from the JSON; `SynthesisSession::run_one_chunk` adds the current
`offset_compensation` to each `BoundaryChunk::offset_ticks` before appending to
the output. Compensation is updated at `turn.end` using
`cumulative_audio_bytes * 8 * 10_000_000 / 48_000` (64-bit integer arithmetic).

**C++ behavior:** `MetadataJsonParser::parse()` collects ALL boundary chunks
from the array into a vector — more correct for frames that could contain
multiple items.

---

# Voice JSON Parsing

**C++ implementation:** `serialization::VoiceJsonParser` (`VoiceJsonParser.hpp` /
`VoiceJsonParser.cpp`).  No HTTP dependency — only operates on a raw JSON string.

**Root type:** JSON array.

**Normalisation applied** (in `VoiceJsonParser`):
- If `VoiceTag` key is absent, default it to `{}`.
- If `VoiceTag.ContentCategories` is absent, default it to `[]`.
- If `VoiceTag.VoicePersonalities` is absent, default it to `[]`.

**Ordering:** `list_voices()` returns voices in wire order.  Sorting by `ShortName`
is performed only in the CLI display layer. `VoiceJsonParser::parse()` preserves
wire order.

**Required fields** (missing any → `parse_error`):
`Name`, `ShortName`, `Gender`, `Locale`, `SuggestedCodec`, `FriendlyName`, `Status`.

**`Gender` values** (case-sensitive): `"Female"`, `"Male"`.  Any other value is
a `parse_error`.

**`Language` derivation:** `Locale.split('-')[0]` — first segment before the
first hyphen.

**Unknown fields:** silently ignored.

---

# VoiceService — HTTP Voice List

**C++ implementation:** `communication::VoiceService` (`VoiceService.hpp` /
`VoiceService.cpp`).

## HTTP request

| Property | Value |
|----------|-------|
| Method | `GET` |
| URL | `config.voices_endpoint` with Sec-MS-GEC query params |
| User-Agent | `config.user_agent` |
| Accept | `*/*` |
| Accept-Language | `en-US,en;q=0.9` |
| Accept-Encoding | `gzip, deflate, br, zstd` |

## Response handling

- 200 → parse body via `VoiceJsonParser` (no JSON code in communication layer)
- Non-200 → `ErrorCode::service_error` with status code in context
- Transport failure → propagated as-is from `IHttpClient`

## Ordering

`list_voices()` returns voices in **wire order** (no sorting). The reference's
`_print_voices()` sorts by `ShortName` for CLI display only — that is the
caller's responsibility.

## Filtering

`VoiceService::list_voices(VoiceFilter)` applies client-side filtering as a
C++ convenience:
- `locale` → exact `Locale` match
- `gender` → exact `Gender` match
- `short_name` → exact `ShortName` match
- All set fields are ANDed

---

# Voice Listing

**Endpoint:**
```
GET https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list
    ?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4
    &Sec-MS-GEC=<token>
    &Sec-MS-GEC-Version=1-143.0.3650.75
```

**Request headers:**

| Header | Value |
|--------|-------|
| `User-Agent` | Chrome/Edge 143 user-agent string (see `docs/PROTOCOL_NOTES.md`) |
| `Accept-Encoding` | `gzip, deflate, br, zstd` |
| `Accept-Language` | `en-US,en;q=0.9` |
| `Authority` | `speech.platform.bing.com` |
| `Sec-CH-UA` | Chromium + Edge 143 brand string |
| `Sec-CH-UA-Mobile` | `?0` |
| `Accept` | `*/*` |
| `Sec-Fetch-Site` | `none` |
| `Sec-Fetch-Mode` | `cors` |
| `Sec-Fetch-Dest` | `empty` |
| `Cookie` | `muid=<16-byte uppercase hex random>;` |

**Response:** JSON array of voice objects.  Each element is normalised to guarantee `VoiceTag.ContentCategories` and `VoiceTag.VoicePersonalities` are present (defaulting to `[]`).

**Wire fields and C++ mapping:**

| Wire field (JSON key) | C++ field | Notes |
|---|---|---|
| `Name` | `voice.name` | Full "Microsoft Server Speech…" form |
| `ShortName` | `voice.short_name` | e.g. `en-US-EmmaMultilingualNeural` |
| `Gender` | `voice.gender` (`VoiceGender` enum) | `unknown` when absent/unrecognised |
| `Locale` | `voice.locale` | e.g. `en-US` |
| `SuggestedCodec` | `voice.suggested_codec` | e.g. `audio-24khz-48kbitrate-mono-mp3` |
| `FriendlyName` | `voice.friendly_name` | Human-readable name |
| `Status` | `voice.status` | `"GA"`, `"Preview"`, or `"Deprecated"` |
| `VoiceTag.ContentCategories` | `voice.content_categories` | Defaulted to `[]` when absent |
| `VoiceTag.VoicePersonalities` | `voice.voice_personalities` | Defaulted to `[]` when absent |
| (derived) | `voice.language` | Derived: `Locale.split('-')[0]` |

**`VoiceGender` wire values** (case-sensitive):

| C++ enum | Wire string |
|----------|-------------|
| `VoiceGender::female` | `"Female"` |
| `VoiceGender::male` | `"Male"` |
| `VoiceGender::unknown` | `"Unknown"` (not a wire value; used for absent/unrecognised) |

The `Language` field is derived from `Locale.split("-")[0]`.

**Retry on 403:** On HTTP 403, inspect the `Date` response header; parse it with
`parse_http_date()` and compute `skew = server_ts - (clock_now + current_skew)`;
call `EdgeTokenProvider::adjust_clock_skew_from_server_timestamp()`.  If the
`Date` header is absent or unparsable, fall back to `adjust_clock_skew(300.0)`.
Retry exactly once with the corrected token.  See DRM section for full algorithm.

---

# Text Validation

Control characters in Unicode ranges `U+0000–U+0008`, `U+000B–U+000C`, and
`U+000E–U+001F` are replaced with ASCII space `U+0020` before processing.
Notably, `U+0009` (tab), `U+000A` (newline), and `U+000D` (carriage return)
are **preserved**.

The replacement is performed **before** XML escaping and before chunking.

**C++ implementation** (`serialization::TextNormalizer`):
- `TextNormalizer::normalize(input)` → `Result<std::string>` validates UTF-8 first
  (rejects invalid sequences), then replaces the documented control ranges.
- CRLF (`\r\n`) is NOT normalised to LF — both bytes are preserved.
- Leading/trailing whitespace is NOT trimmed (per-chunk stripping happens in the chunker).
- Empty input returns `Result::ok("")`.

**`xml_escape` / `xml_unescape`** (`serialization::XmlEscaper`):
- `xml_escape` maps `&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`.  Does NOT escape `"` or `'`.
- `xml_escape` is NOT idempotent: `xml_escape("&amp;")` → `"&amp;amp;"`.
- `xml_unescape` maps `&amp;`→`&`, `&lt;`→`<`, `&gt;`→`>`, `&quot;`→`"`, `&apos;`→`'`.
  Unknown entities are passed through unchanged.
- Pipeline order: `TextNormalizer::normalize()` → `xml_escape()` → chunker.

---

# Text Chunking

**Limit:** 4096 bytes per chunk (after UTF-8 encoding + XML escaping).

**Algorithm (in priority order):**

1. Text is encoded to UTF-8 bytes.
2. While remaining bytes > 4096:
   a. Search backward from byte 4096 for the rightmost `\n` (newline preferred
      over space).
   b. If no newline, search backward for the rightmost ` ` (space).
   c. If neither found, find the rightmost safe UTF-8 boundary by decoding
      progressively shorter prefixes.
   d. Adjust backward to avoid splitting inside an XML entity (`&…;`): if there
      is an unterminated `&` before the split point, move split point to the `&`.
3. Yield the stripped chunk (leading/trailing whitespace removed from each
   yielded chunk).

`ErrorCode::invalid_argument` is returned if `byte_length ≤ 0` or if a valid
split cannot be determined.

**Important:** Chunking operates on the **XML-escaped** text, not the raw text.
Characters like `<`, `>`, `&`, `"`, `'` are escaped before chunking.

**Implementation note:** The C++ `serialization::TextChunker` walks back from
the byte limit when finding a safe UTF-8 split point, which is more correct
than relying on spaces always being present within the window.

**C++ implementation:** `serialization::TextChunker` (`TextChunker.hpp` /
`TextChunker.cpp`) implements the complete reference algorithm:
- `TextChunkerOptions::max_chunk_size = 4096` (reference default)
- `TextChunkerOptions::size_after_xml_escape = true` (reference behavior)
- `TextChunkerOptions::prefer_sentence_boundary = true` (newline preference)
- `TextChunkerOptions::prefer_word_boundary = true` (space fallback)
- Returns XML-escaped, stripped chunks ready for SSML `<prosody>` embedding.
- Propagates UTF-8 validation errors from `TextNormalizer`.

---

# SSML Generation

**C++ implementation:** `serialization::SsmlBuilder` (`SsmlBuilder.hpp` / `SsmlBuilder.cpp`).

**Entry points and escaping contract:**

`SsmlBuilder` exposes two entry points to keep escaping unambiguous:

| Entry point | Input contract | Use case |
|-------------|---------------|----------|
| `build(config, raw_text)` | Raw user text — normalized + XML-escaped once inside | Direct callers with unescaped text |
| `build_from_escaped_text(config, escaped_text)` | Already XML-escaped text — embedded verbatim | `EdgeProtocol::build_ssml_frame`, which receives pre-escaped chunks from `TextChunker` |

Passing raw text to `build_from_escaped_text` produces malformed SSML.
Passing already-escaped text to `build` produces double-escaped entities (`&amp;amp;`).

**SSML body template:**
```xml
<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>
  <voice name='{voice_full_name}'>
    <prosody pitch='{pitch}' rate='{rate}' volume='{volume}'>
      {xml_escaped_text_chunk}
    </prosody>
  </voice>
</speak>
```

All on one line (no formatting whitespace in the actual output).

**Message envelope** sent over WebSocket as a text frame:
```
X-RequestId:{request_id}\r\n
Content-Type:application/ssml+xml\r\n
X-Timestamp:{timestamp}Z\r\n
Path:ssml\r\n
\r\n
{ssml_body}
```

Note the literal `Z` suffix appended to the timestamp regardless of timezone
(documented in the source as a known Microsoft Edge bug).

**Timestamp format:** JavaScript-style date string, always in UTC:
`Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)`.

**Request ID:** UUID v4 without dashes (32 hex chars).

---

# WebSocket Protocol

**WebSocket URL:**
```
wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
  ?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4
  &ConnectionId=<uuid_no_dashes>
  &Sec-MS-GEC=<sha256_token>
  &Sec-MS-GEC-Version=1-143.0.3650.75
```

**WebSocket request headers:**

| Header | Value |
|--------|-------|
| `Pragma` | `no-cache` |
| `Cache-Control` | `no-cache` |
| `Origin` | `chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold` |
| `Sec-WebSocket-Version` | `13` |
| `User-Agent` | Chrome/Edge 143 user-agent |
| `Accept-Encoding` | `gzip, deflate, br, zstd` |
| `Accept-Language` | `en-US,en;q=0.9` |
| `Cookie` | `muid=<16-byte uppercase hex random>;` |

WebSocket compression is enabled (`compress=15`).

**Per-text-chunk connection lifecycle:**
1. Open a new WebSocket connection per text chunk.
2. Send `speech.config` text frame (see below).
3. Send `ssml` text frame.
4. Receive frames until `turn.end` is received.
5. Connection is closed after each chunk.

**`speech.config` message** (text frame):
```
X-Timestamp:{timestamp}\r\n
Content-Type:application/json; charset=utf-8\r\n
Path:speech.config\r\n
\r\n
{"context":{"synthesis":{"audio":{"metadataoptions":{
  "sentenceBoundaryEnabled":"{sq}","wordBoundaryEnabled":"{wd}"},
  "outputFormat":"audio-24khz-48kbitrate-mono-mp3"}}}}
```

**Output format:** `"audio-24khz-48kbitrate-mono-mp3"` is **hardcoded** — there
is no `--format` CLI flag. The C++ `OutputFormat` type enforces this restriction:
`OutputFormat::from_string()` rejects any value not in the known supported set,
and `OutputFormat::default_format()` returns this format.
See `core::OutputFormat` in `docs/MODULES.md`.

Where `wd = "true"` and `sq = "false"` when `boundary == "WordBoundary"`, and
vice versa for `SentenceBoundary`.

**Incoming text frames:**

| `Path` header | Action |
|---------------|--------|
| `audio.metadata` | Parse JSON body; yield `WordBoundary` or `SentenceBoundary` chunk |
| `turn.end` | Compute offset compensation from audio bytes; break loop |
| `response` | Silently ignored |
| `turn.start` | Silently ignored |
| anything else | `protocol_error` |

**Text frame parsing:** `find("\r\n\r\n")` locates the header/body boundary;
headers are split on `\r\n` then on the first `:`.

**Incoming binary frames:**

Binary frame layout:
```
[HL_MSB, HL_LSB, header_content (HL-2 bytes), \r\n, body...]
```

- Bytes 0–1: `HL` = big-endian uint16 = length prefix (2 bytes) + header content length.
- Bytes `[2 .. HL)`: header content in `Key:Value\r\nKey:Value` format (no trailing CRLF).
- Bytes `[HL .. HL+2)`: `\r\n` separator.
- Bytes `[HL+2 ..)`: audio payload.
- `Path: audio` is the only expected binary path.
- `Content-Type: audio/mpeg` is expected; absent `Content-Type` with empty body is
  silently skipped (stream termination signal); any other value is `protocol_error`.

**C++ header parsing:** starts from byte 2 only — all headers parse correctly
regardless of order.

**C++ implementation:** `communication::EdgeProtocol::parse_incoming(WebSocketMessage)`
→ `Result<vector<IncomingMessage>>`. Defined in `src/communication/EdgeProtocolIncoming.cpp`.
Types: `WebSocketMessage` (in/out), `IncomingMessage` + `IncomingMessageKind` (parsed result).

**C++ validation (strict):**

1. `HL < 2` → `protocol_error`.
2. `HL + 2 > len(data)` (missing separator) → `protocol_error`.
3. `data[HL..HL+2) != \r\n` (wrong separator bytes) → `protocol_error`.

All three reject frames the service would never produce, making malformed input
fail loudly rather than silently. See PROTOCOL_NOTES.md for the full binary frame
validation table.

---

# EdgeTokenProvider — Sec-MS-GEC Generation

**C++ implementation:** `communication::EdgeTokenProvider`. SHA-256 helper:
`common::sha256_hex_upper`.

**No wall-clock dependency in tests:** `EdgeTokenProvider` accepts `const IClock&`,
allowing `common::FixedClock` for deterministic tests.

See `PROTOCOL_NOTES.md` — Sec-MS-GEC Token Generation — for test vectors.

---

# DRM / Sec-MS-GEC

**Trusted client token:** `6A5AA1D4EAFF4E9FB37E23D68491D6F4`

**`Sec-MS-GEC` token generation algorithm:**

1. Get current UTC Unix timestamp (float), add accumulated `clock_skew_seconds`.
2. Add Windows file time epoch offset: `11644473600` seconds (to convert Unix →
   Windows file time seconds since 1601-01-01).
3. Round down to nearest 300-second (5-minute) boundary: `ticks -= ticks % 300`.
4. Convert to 100-nanosecond intervals: multiply by `1e9 / 100`.
5. Concatenate as integer string with the trusted client token:
   `f"{ticks:.0f}6A5AA1D4EAFF4E9FB37E23D68491D6F4"`.
6. SHA-256 hash the ASCII-encoded string; return uppercase hex digest.

**`Sec-MS-GEC-Version`:** `1-143.0.3650.75` (constant).

**MUID cookie:** `muid=<32-uppercase-hex-chars>;` — a new random 16-byte
hex token is generated per request.  Added to all voice-list and WebSocket
requests.

**Clock skew correction:** The accumulated clock skew starts at `0.0` seconds.
When a `403` response is received, the RFC 2616 `Date` response header is
parsed and `skew += server_date - client_date`.

**Float precision:** The `ticks` multiplication uses 64-bit double arithmetic
(`ticks * 1e9 / 100`), formatted with `%.0f` (round-to-nearest). This must
produce bit-identical token strings across platforms.

---

# Stream Chunk Types

Three chunk types flow from `SpeechSynthesizer::synthesize()`:

| Type | Additional fields | Wire origin |
|------|-------------------|-------------|
| `audio` | `data: bytes` (raw MP3 frames) | Binary WebSocket frame |
| `WordBoundary` | `offset: ticks`, `duration: ticks`, `text: str` | `audio.metadata` JSON |
| `SentenceBoundary` | `offset: ticks`, `duration: ticks`, `text: str` | `audio.metadata` JSON |

**Offset and duration units:** 100-nanosecond ticks.
`1 second = 10,000,000 ticks` (constant `TICKS_PER_SECOND = 10_000_000`).

**Tick-to-microsecond conversion:**
```
microseconds = ticks / 10
```

**Offset compensation:** Between text chunks, the cumulative audio byte count
is used to compute an offset correction:
```
offset_compensation = cumulative_audio_bytes * 8 * 10_000_000 // 48_000
```
This uses **integer floor division** and is computed from the `audio-24khz-48kbitrate-mono-mp3`
CBR stream (48 kbps = 48,000 bps).  Each reported boundary offset is summed
with `offset_compensation` before yielding.

**`text` field:** XML-unescaped from the JSON `Data.text.Text` field.

**Single-use:** `SpeechSynthesizer::synthesize()` and `save()` are both
single-use. Calling either a second time returns `ErrorCode::invalid_state`.

**C++ chunk types** (`include/edge_tts/core/Chunk.hpp`):

| C++ type | Key fields |
|----------|------------|
| `AudioChunk` | `data: vector<byte>` |
| `BoundaryChunk` (`type = BoundaryEventType::WordBoundary`) | `text`, `offset_ticks`, `duration_ticks` |
| `BoundaryChunk` (`type = BoundaryEventType::SentenceBoundary`) | `text`, `offset_ticks`, `duration_ticks` |

`TtsChunk = std::variant<AudioChunk, BoundaryChunk>`.  Use `is_audio()` and
`is_boundary()` predicates to distinguish.  Zero-duration boundary chunks
are valid (some sentence boundaries have `duration = 0`).

---

# SubtitleBuilder — Boundary Event Accumulation

**C++ implementation:** `subtitles::SubtitleBuilder` (`SubtitleBuilder.hpp` / `SubtitleBuilder.cpp`).

## Public methods

| Method | Signature | Notes |
|--------|-----------|-------|
| `feed` | `feed(BoundaryChunk&)→Result<void>` | Appends a cue; enforces type consistency |
| `to_srt` | `to_srt()→Result<string>` | Delegates to `SrtComposer`; does not reset state |
| `cues` | `cues()→vector<SubtitleCue>` | Returns copy |
| `clear` | `clear()` | Resets cues and type lock |

## Type enforcement

The first boundary type seen is recorded. Subsequent calls that supply a
different type return `ErrorCode::invalid_argument`. Both `WordBoundary` and
`SentenceBoundary` are accepted, but all feeds to one `SubtitleBuilder` instance
must use the same type.

## Cue time calculation

```cpp
start = SubtitleTime::from_edge_ticks(boundary.offset_ticks)
end   = SubtitleTime::from_edge_ticks(boundary.offset_ticks + boundary.duration_ticks)
```

## Text storage

`boundary.text` is stored verbatim. `MetadataJsonParser` has already applied
`xml_unescape()`. No text transformation happens in `SubtitleBuilder`.

## State after `to_srt()`

`to_srt()` does not modify state — calling it multiple times returns the same
output, and `feed()` can continue adding cues afterward.

## Zero-duration cues

A cue with `duration_ticks == 0` has `start == end`. `SubtitleBuilder::feed()` accepts
it (creating the cue), but `SrtComposer` skips it in SRT output because
`start >= end`.

---

# SubtitleTime — Edge Tick Conversion

**C++ implementation:** `subtitles::SubtitleTime` (`SubtitleTime.hpp` / `SubtitleTime.cpp`).

## Tick-to-millisecond conversion

`SubtitleTime::from_edge_ticks(ticks)`:
```cpp
milliseconds = ticks / 10'000  // integer truncation
```
This is equivalent to `ticks // 10 // 1000` (integer truncation). A ±1 ms
rounding difference can occur only when `ticks % 10'000 ≥ 9'995` — extremely
rare in practice. Callers should not rely on sub-millisecond precision.

**Negative ticks:** `from_edge_ticks` returns `ErrorCode::invalid_argument`.
The SRT composer skips subtitles whose start time is negative, so this is
consistent with the output behavior.

## SRT timestamp format

```
HH:MM:SS,mmm
```

- **Comma** separator between seconds and milliseconds (not a dot).
- Each component is zero-padded: HH minimum 2 digits, MM exactly 2, SS exactly 2,
  mmm exactly 3.
- Hours are **not** capped at 99 — values ≥ 100 hours expand the hours field.

Example: `01:23:04,000` for 1 hour, 23 minutes, 4 seconds.

---

# Subtitles

**`SubtitleBuilder::feed(chunk)`:**
- Accepts only `WordBoundary` or `SentenceBoundary` chunks.
- All chunks in one session must share the same type; mixing types returns
  `ErrorCode::invalid_argument`.
- Converts ticks: divide by 10 for microseconds, divide again by 1000 for
  milliseconds.
- End time = start_ticks + duration_ticks, then converted.
- Appends a cue (index, start, end, content) to the internal list.

**SRT format produced by `SrtComposer::compose()`:**
```
{index}
{HH:MM:SS,mmm} --> {HH:MM:SS,mmm}
{content}

```

- Index is 1-based and recomputed by `sort_and_reindex()` (sorted by start time).
- Subtitles with empty content, negative start, or start ≥ end are **skipped**.
- Timestamp: `HH:MM:SS,mmm` (comma separator between seconds and milliseconds).
- Each block ends with two newlines.


---

# Playback

`edge-playback` orchestrates:
1. Parse args (strips `--mpv` flag from the arg list before forwarding).
2. Check that `edge-tts` (and `mpv` on non-Windows) are on `$PATH`.
3. Create temp `.mp3` and (when using mpv) `.srt` files.
4. Call `edge-tts --write-media=<tmp.mp3> [--write-subtitles=<tmp.srt>] <remaining args>` as subprocess.
5. Play with `mpv` (`--sub-file=<tmp.srt>`) or on Windows via the
   `win32_playback.play_mp3_win32` function.
6. Clean up temp files unless `EDGE_PLAYBACK_KEEP_TEMP` env var is set.

**Environment variables:**

| Variable | Effect |
|----------|--------|
| `EDGE_PLAYBACK_DEBUG` | Print temp file paths to stdout |
| `EDGE_PLAYBACK_KEEP_TEMP` | Keep temp files after playback |
| `EDGE_PLAYBACK_MP3_FILE` | Override MP3 temp file path |
| `EDGE_PLAYBACK_SRT_FILE` | Override SRT temp file path |

**mpv command:** `mpv --msg-level=all=error,statusline=status [--sub-file=<srt>] <mp3>`

**Windows:** Uses `win32_playback.play_mp3_win32` (no mpv required by default on Windows).

Linux/macOS uses mpv + temp files. Windows playback is best-effort; the `--mpv`
flag must be supported.

---

# Proxy Behavior

A proxy URL string is accepted by both `SpeechSynthesizer` (for WebSocket) and
`list_voices()` (for HTTPS). The value is forwarded to the HTTP/WebSocket client
without validation or modification.

---

# Retry / Clock Skew Behavior

**Trigger condition:** HTTP 403 response from the service.

**Retry flow (both WebSocket and voice list):**

1. On `403`, read the `Date` response header (RFC 2616 format), parse it,
   compute `server_date - client_date`, and add the difference to
   the accumulated `clock_skew_seconds`.
2. If the `Date` header is absent or unparseable, adjust skew by a fixed
   fallback (see below) or skip adjustment.
3. Immediately retry the **same request** once with a freshly generated
   `Sec-MS-GEC` token (incorporating the updated clock skew).

**No exponential backoff:** exactly one retry per 403.  Any subsequent failure
is propagated.

**C++ implementation status:** Implemented.

**WebSocket path (`SynthesisSession`):**
- `WebSocketClient::connect()` maps HTTP 403 → `ErrorCode::drm_error` and stores the
  `Date` response header (from `ix::WebSocketInitResult::headers`) as `error.context()`.
- `SynthesisSession` retry path: if `should_retry()` returns true, calls
  `parse_http_date(error.context())`, computes
  `skew = server_time - (client_now + existing_skew)`, then calls
  `token_provider_.adjust_clock_skew(skew)` before retrying with a new ConnectionId
  and freshly computed `Sec-MS-GEC`.
- If the Date header is absent or malformed, skew adjustment is skipped (retry
  still proceeds without correction).

**Voice-list path (`VoiceService`):**
- `send_request()` returns the raw `HttpResponse`; on HTTP 403, `list_voices()`
  inspects `resp.headers["Date"]`.
- Calls `EdgeTokenProvider::adjust_clock_skew_from_server_timestamp(server_ts)`, which
  computes `skew = server_ts - (clock_now + current_skew)` and accumulates it.
- **Fallback:** if `Date` is absent or unparsable, calls `adjust_clock_skew(300.0)`
  as a conservative correction.

**Shared:**
- `parse_http_date()` is in `communication/HttpDate.hpp`; format:
  `"Wkd, DD Mon YYYY HH:MM:SS GMT"` (RFC 2616).
- `EdgeTokenProvider::clock_skew_seconds()` is per-instance (injectable for tests).

---

# Error Behavior

**C++ `ErrorCode` values used for each condition:**

| Condition | `ErrorCode` |
|-----------|-------------|
| Unknown metadata type or unknown WebSocket path | `protocol_error` |
| Unexpected but known-format response (protocol violation) | `protocol_error` |
| Stream completed without yielding any audio chunks | `service_error` |
| WebSocket-level transport error | `network_error` |
| 403 handling when Date header is absent or malformed | skew skipped; retry proceeds |

---

# Service Constants

| Property | Value |
|----------|-------|
| Chromium version string | `143.0.3650.75` |
| `SEC_MS_GEC_VERSION` header | `1-143.0.3650.75` |
| `TRUSTED_CLIENT_TOKEN` | `6A5AA1D4EAFF4E9FB37E23D68491D6F4` |
| Audio output format | `audio-24khz-48kbitrate-mono-mp3` |
| Audio bitrate (for timing) | 48,000 bps |
| Tick resolution | 100 nanoseconds (10,000,000 ticks/second) |

The Chromium version and trusted token are likely to change when Microsoft
updates the Edge TTS service.  Keep these as named constants in
`EdgeServiceConfig`, not hard-coded literals scattered through the code.

---

# Compatibility Targets

| Requirement | Value |
|-------------|-------|
| C++ standard | C++20 |
| Compilers tested | GCC ≥ 12, Clang ≥ 15, MSVC ≥ 19.38 (VS 2022 17.8) |
| Platforms | Linux, macOS, Windows |
| TLS | OpenSSL (Linux/macOS), mbedtls via ixwebsocket (Windows) |
| Build system | CMake ≥ 3.21 |

---

# Ambiguities / Requires Live Verification

The following behaviors are inferred from the code but have not been verified
against the live service:

| # | Topic | Observation | Open Question |
|---|-------|-------------|---------------|
| 1 | Voice name regex | The regex `\(.+,.+\)` does not require a space after the comma, but the generated string always includes one: `(en-US, Emma…)`. | Does the service accept the no-space form? |
| 2 | `speech.config` per chunk | A new WebSocket connection is opened per text chunk; `speech.config` is re-sent each time. | Can connections be reused across chunks? |
| 3 | Offset compensation integer overflow | Microsoft's metadata offsets can overflow on long texts; the CBR-byte compensation approach avoids this. | Is 64-bit integer arithmetic sufficient for arbitrarily long audio? |
| 4 | `turn.end` binary | The code expects `turn.end` as a text frame and processes offset compensation there. | Does the service ever send `turn.end` as binary? |
| 5 | Empty binary with no Content-Type | The code silently skips zero-length binary frames with no Content-Type. | Is this a documented termination signal or just observed empirically? |
| 6 | `SessionEnd` metadata | `SessionEnd` metadata type is silently skipped. | Are there other metadata types not yet observed? |
| 7 | Retry count | Only one retry per 403 is implemented. | Does the service ever require more than one clock-skew correction per session? |
| 8 | `--write-media -` | Writing MP3 to stdout is supported (the `-` flag). | How is this tested without a live service? |
| 9 | Determinism | A 26-parallel-process test compares SRT outputs for equality. | Are SRT outputs truly deterministic across connections, or is there sentence-boundary jitter? |
| 10 | DRM float precision | Token generation multiplies by `1e9 / 100` using 64-bit double. | Does IEEE 754 double produce the same `.0f`-formatted integer as the service expects on all platforms? |
