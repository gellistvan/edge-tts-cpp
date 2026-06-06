# Reference Behavior Inventory

This document is the authoritative compatibility reference for `edge-tts-cpp`.
Every future implementation task that touches networking, protocol handling,
text processing, or audio timing **must start here** before making design
decisions.  If a behavior described below is ambiguous or requires live service
verification, that is noted explicitly.

---

# Reference Files Inspected

| File | Purpose |
|------|---------|
| `reference/edge-tts/README.md` | End-user installation, CLI examples, voice listing output |
| `reference/edge-tts/src/edge_tts/__init__.py` | Public API surface |
| `reference/edge-tts/src/edge_tts/communicate.py` | Core TTS engine: text chunking, SSML, WebSocket protocol, chunk parsing |
| `reference/edge-tts/src/edge_tts/constants.py` | All network endpoints, header values, DRM constants, timing constants |
| `reference/edge-tts/src/edge_tts/data_classes.py` | `TTSConfig` dataclass and validation regexes |
| `reference/edge-tts/src/edge_tts/drm.py` | `Sec-MS-GEC` token generation, MUID cookie, clock-skew correction |
| `reference/edge-tts/src/edge_tts/exceptions.py` | Exception hierarchy |
| `reference/edge-tts/src/edge_tts/submaker.py` | `SubMaker`: accumulates boundary events into SRT subtitle cues |
| `reference/edge-tts/src/edge_tts/srt_composer.py` | `Subtitle` type, SRT timestamp formatting, `compose()` |
| `reference/edge-tts/src/edge_tts/util.py` | CLI argument parsing, `_run_tts`, `_print_voices` |
| `reference/edge-tts/src/edge_tts/typing.py` | `TTSChunk`, `Voice`, `CommunicateState` TypedDicts |
| `reference/edge-tts/src/edge_tts/voices.py` | `list_voices()`, `VoicesManager` |
| `reference/edge-tts/src/edge_playback/__main__.py` | `edge-playback` CLI, mpv / win32 playback, temp-file lifecycle |
| `reference/edge-tts/src/edge_playback/util.py` | `pr_err` helper |
| `reference/edge-tts/setup.cfg` | Entry points, Python `>=3.7` requirement, dependencies |
| `reference/edge-tts/setup.py` | Runtime dependency versions |
| `reference/edge-tts/tests/001-long-text.sh` | 26-parallel-process determinism test |
| `reference/edge-tts/tests/001-long-text.txt` | 209,322-byte Wikipedia article as test input |

---

# Public Python API

**Source:** `reference/edge-tts/src/edge_tts/__init__.py`

The Python package exports exactly:

```python
Communicate, SubMaker, list_voices, VoicesManager, exceptions,
__version__, __version_info__
```

**C++ equivalents to implement:**

| Python symbol | C++ target |
|---------------|-----------|
| `Communicate` | `edge_tts::api::Communicate` |
| `SubMaker` | `edge_tts::subtitles::SubMaker` |
| `list_voices()` | `edge_tts::communication::HttpVoiceService::list_voices()` |
| `VoicesManager` | `edge_tts::communication::VoicesManager` |

**Match exactly:** Yes.

---

# CLI Commands

**Source:** `reference/edge-tts/setup.cfg` (entry points), `reference/edge-tts/src/edge_tts/util.py`, `reference/edge-tts/src/edge_playback/__main__.py`

Two CLI commands are registered:

| Command | Entry point |
|---------|-------------|
| `edge-tts` | `edge_tts.__main__:main` → `util.py:main()` |
| `edge-playback` | `edge_playback.__main__:_main` |

`edge-playback` delegates all TTS work to `edge-tts` via subprocess.  It accepts
all `edge-tts` options except `--write-media`, `--write-subtitles`, and
`--list-voices`.

**Match exactly:** Yes for both commands.

---

# CLI Options

**Source:** `reference/edge-tts/src/edge_tts/util.py` (`amain()`)

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

**Match exactly:** Yes.

---

# Default Values

**Sources:** `reference/edge-tts/src/edge_tts/constants.py`, `reference/edge-tts/src/edge_tts/communicate.py`

| Parameter | Default | Python source |
|-----------|---------|---------------|
| `voice` | `en-US-EmmaMultilingualNeural` | `constants.py: DEFAULT_VOICE` |
| `rate` | `+0%` | `communicate.py` constructor |
| `volume` | `+0%` | `communicate.py` constructor |
| `pitch` | `+0Hz` | `communicate.py` constructor |
| `boundary` | `SentenceBoundary` | `communicate.py` constructor |
| `output_format` | `audio-24khz-48kbitrate-mono-mp3` | `communicate.py` speech.config |
| `connect_timeout` | `10` seconds | `communicate.py` constructor |
| `receive_timeout` | `60` seconds | `communicate.py` constructor |

**Match exactly:** Yes — `TtsConfig::defaults()` returns a struct with all of these values.

### Validation patterns (from `data_classes.py`)

| Field | Python regex | Accepted examples | Rejected examples |
|-------|-------------|-------------------|-------------------|
| `rate` | `^[+-]\d+%$` | `+0%`, `-50%`, `+100%` | `fast`, `++10%`, `10`, `+10`, `+10percent` |
| `volume` | `^[+-]\d+%$` | `+0%`, `-50%`, `+100%` | `loud`, `10%`, `+10percent` |
| `pitch` | `^[+-]\d+Hz$` | `+0Hz`, `-50Hz`, `+100Hz` | `high`, `+10%`, `10Hz`, `+10` |
| `voice` | short or full form | `en-US-EmmaMultilingualNeural`, full "Microsoft Server Speech..." | empty, `not-a-voice` |

### Boundary type wire strings

| C++ enum | Python literal | Wire value |
|----------|---------------|------------|
| `BoundaryType::sentence` | `"SentenceBoundary"` | `sentenceBoundaryEnabled: "true"` |
| `BoundaryType::word` | `"WordBoundary"` | `wordBoundaryEnabled: "true"` |

---

# Edge Service Constants

**Sources:** `reference/edge-tts/src/edge_tts/constants.py`,
`communicate.py`, `drm.py`, `voices.py`

**C++ implementation:** `communication::EdgeServiceConfig` struct +
`default_edge_service_config()` factory in
`src/communication/EdgeServiceConfig.cpp`.

All Edge TTS hard-coded constants are centralized here. Networking and
serialization code must receive them via `EdgeServiceConfig`, not inline them.

See `docs/PROTOCOL_NOTES.md` — Service Constants section — for the full value
table.

**Match exactly:** Yes — all values derived verbatim from the reference.

---

# Protocol Text Frame Parsing and Serialization

**Sources:** `reference/edge-tts/src/edge_tts/communicate.py`
(`ssml_headers_plus_data`, `send_command_request`, `get_headers_and_data`)

**C++ implementation:** `serialization::ProtocolMessage`, `serialization::ProtocolParser`,
`serialization::ProtocolSerializer`.

**Wire format:** `Name:Value\r\n` per header, `\r\n\r\n` separator, body verbatim.
No space between header name and colon; no space between colon and value.

**Parsing divergence from reference:** The reference uses `data.find(b"\r\n\r\n") + 2`
for the body offset, leaving a leading `\r\n` that Python's `json.loads` ignores.
The C++ parser uses `+ 4` to cleanly skip the full separator.

**Header ordering:** Preserved in wire order. Duplicate header names are kept as
separate entries (unlike the Python dict which overwrites earlier values).

**LF-only frames:** Rejected — reference splits exclusively on `\r\n`.

**Match exactly (serializer):** Yes — format matches Python output character-for-character.
**Match exactly (parser):** Functionally equivalent — same result, cleaner body offset.

---

# Metadata JSON Parsing

**Sources:** `reference/edge-tts/src/edge_tts/communicate.py` (`__parse_metadata`)

**C++ implementation:** `serialization::MetadataJsonParser`

**JSON root:** Object with `"Metadata"` array.

**Handled types:** `"WordBoundary"` → `BoundaryEventType::WordBoundary`;
`"SentenceBoundary"` → `BoundaryEventType::SentenceBoundary`.

**Skipped types:** `"SessionEnd"` (reference: `continue`).

**Unknown types:** `parse_error` (reference: `UnknownResponse`).

**XML unescape:** Applied to `Data.text.Text` field before storing in `BoundaryChunk::text`.

**Offset compensation:** NOT applied in the parser. `MetadataJsonParser` returns
raw ticks from the JSON; `SynthesisSession::run_one_chunk` adds the current
`offset_compensation` to each `BoundaryChunk::offset_ticks` before appending to
the output — matching `__parse_metadata`'s `current_offset = raw + offset_compensation`.
Compensation is updated at `turn.end` using
`cumulative_audio_bytes * 8 * 10_000_000 / 48_000` (64-bit integer arithmetic).

**C++ vs Python difference:** The Python `__parse_metadata` returns on the FIRST
handled item. The C++ `MetadataJsonParser::parse()` collects ALL boundary chunks
from the array into a vector — more correct for frames that could contain
multiple items.

**Match exactly:** Functionally equivalent. Same event handling, same XML unescape,
same error conditions.

---

# Voice JSON Parsing

**Sources:** `reference/edge-tts/src/edge_tts/voices.py`, `reference/edge-tts/src/edge_tts/typing.py`

**C++ implementation:** `serialization::VoiceJsonParser` (`VoiceJsonParser.hpp` /
`VoiceJsonParser.cpp`).  No HTTP dependency — only operates on a raw JSON string.

**Root type:** JSON array (reference `list_voices()` calls `json.loads()` on the
response body and treats the result as a list).

**Normalisation applied by `voices.py`** (replicated in `VoiceJsonParser`):
- If `VoiceTag` key is absent, default it to `{}`.
- If `VoiceTag.ContentCategories` is absent, default it to `[]`.
- If `VoiceTag.VoicePersonalities` is absent, default it to `[]`.

**Ordering:** `list_voices()` returns the array in the order the service sends
it.  Sorting by `ShortName` is performed only in `_print_voices()` for CLI
display.  `VoiceJsonParser::parse()` therefore preserves wire order.

**Required fields** (missing any → `parse_error`):
`Name`, `ShortName`, `Gender`, `Locale`, `SuggestedCodec`, `FriendlyName`, `Status`.

**`Gender` values** (case-sensitive): `"Female"`, `"Male"`.  Any other value is
a `parse_error`.

**`Language` derivation:** `Locale.split('-')[0]` — first segment before the
first hyphen.

**Unknown fields:** silently ignored (reference accesses only known keys).

**Match exactly:** Yes.

---

# VoiceService — HTTP Voice List

**Sources:** `voices.py`, `util.py`, `constants.py`

**C++ implementation:** `communication::VoiceService` (`VoiceService.hpp` /
`VoiceService.cpp`).

## HTTP request

| Property | Reference | C++ |
|----------|-----------|-----|
| Method | `session.get(...)` | `"GET"` |
| URL | `VOICE_LIST + &Sec-MS-GEC=... + &Sec-MS-GEC-Version=...` | `config.voices_endpoint` (Sec-MS-GEC added in future) |
| User-Agent | `BASE_HEADERS["User-Agent"]` | `config.user_agent` |
| Accept | `VOICE_HEADERS["Accept"] = "*/*"` | `"*/*"` |
| Accept-Language | `BASE_HEADERS["Accept-Language"] = "en-US,en;q=0.9"` | `"en-US,en;q=0.9"` |
| Accept-Encoding | `BASE_HEADERS["Accept-Encoding"] = "gzip, deflate, br, zstd"` | `"gzip, deflate, br, zstd"` |

## Response handling

- 200 → parse body via `VoiceJsonParser` (no JSON code in communication layer)
- Non-200 → `ErrorCode::service_error` with status code in context
- Transport failure → propagated as-is from `IHttpClient`

## Ordering

`list_voices()` returns voices in **wire order** (no sorting). The reference's
`_print_voices()` sorts by `ShortName` for CLI display only — that is the
caller's responsibility.

## Filtering

The reference `list_voices()` returns all voices; filtering is done by
`VoicesManager.find()` separately. `VoiceService::list_voices(VoiceFilter)`
applies client-side filtering as a C++ convenience:
- `locale` → exact `Locale` match
- `gender` → exact `Gender` match
- `short_name` → exact `ShortName` match
- All set fields are ANDed

**Match exactly:** Yes for fetch/parse/ordering. `VoiceFilter` is a C++ extension.

---

# Voice Listing

**Sources:** `reference/edge-tts/src/edge_tts/voices.py`, `reference/edge-tts/src/edge_tts/constants.py`

**Endpoint:**
```
GET https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list
    ?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4
    &Sec-MS-GEC=<token>
    &Sec-MS-GEC-Version=1-143.0.3650.75
```

**Request headers** (`VOICE_HEADERS` merged with `BASE_HEADERS`):

| Header | Value |
|--------|-------|
| `User-Agent` | Chrome/Edge 143 user-agent string (see constants.py) |
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

**`Voice` TypedDict fields** (`reference/edge-tts/src/edge_tts/typing.py`) and C++ mapping:

| Python field | Python type | C++ field | Notes |
|---|---|---|---|
| `Name` | `str` | `voice.name` | Full "Microsoft Server Speech…" form |
| `ShortName` | `str` | `voice.short_name` | e.g. `en-US-EmmaMultilingualNeural` |
| `Gender` | `"Female"` \| `"Male"` | `voice.gender` (`VoiceGender` enum) | `unknown` when absent/unrecognised |
| `Locale` | `str` | `voice.locale` | e.g. `en-US` |
| `SuggestedCodec` | `str` | `voice.suggested_codec` | e.g. `audio-24khz-48kbitrate-mono-mp3` |
| `FriendlyName` | `str` | `voice.friendly_name` | Human-readable name |
| `Status` | `"GA"` \| `"Preview"` \| `"Deprecated"` | `voice.status` | |
| `VoiceTag.ContentCategories` | `List[str]` | `voice.content_categories` | Defaulted to `[]` by voices.py |
| `VoiceTag.VoicePersonalities` | `List[str]` | `voice.voice_personalities` | Defaulted to `[]` by voices.py |
| `Language` (VoicesManagerVoice) | `str` | `voice.language` | Derived: `Locale.split('-')[0]` |

**`VoiceGender` wire values** (case-sensitive, matching Python's `Literal["Female", "Male"]`):

| C++ enum | Wire string |
|----------|-------------|
| `VoiceGender::female` | `"Female"` |
| `VoiceGender::male` | `"Male"` |
| `VoiceGender::unknown` | `"Unknown"` (not a wire value; used for absent/unrecognised) |

`VoicesManager.create()` adds a synthetic `Language` field derived as
`Locale.split("-")[0]`.

**Retry on 403:** uses the same clock-skew correction as WebSocket (see DRM section).

**Match exactly:** Yes — same endpoint, same headers including MUID cookie.

---

# Text Validation

**Source:** `reference/edge-tts/src/edge_tts/communicate.py` (`remove_incompatible_characters`)

Control characters in Unicode ranges `U+0000–U+0008`, `U+000B–U+000C`, and
`U+000E–U+001F` are replaced with ASCII space `U+0020` before processing.
Notably, `U+0009` (tab), `U+000A` (newline), and `U+000D` (carriage return)
are **preserved**.

The replacement is performed **before** XML escaping with
`xml.sax.saxutils.escape()` and before chunking.

The `text` argument to `Communicate` must be `str`; passing anything else raises
`TypeError`.

**Match exactly:** Yes — same replacement ranges.

**C++ implementation** (`serialization::TextNormalizer`):
- `TextNormalizer::normalize(input)` → `Result<std::string>` validates UTF-8 first
  (rejects invalid sequences), then replaces the Python-documented control ranges.
- CRLF (`\r\n`) is NOT normalised to LF — both bytes are preserved.
- Leading/trailing whitespace is NOT trimmed (per-chunk stripping happens in the chunker).
- Empty input returns `Result::ok("")`.

**`xml_escape` / `xml_unescape`** (`serialization::XmlEscaper`):
- `xml_escape` maps `&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`.  Does NOT escape `"` or `'`
  (matching `xml.sax.saxutils.escape` exactly).
- `xml_escape` is NOT idempotent: `xml_escape("&amp;")` → `"&amp;amp;"`.
- `xml_unescape` maps `&amp;`→`&`, `&lt;`→`<`, `&gt;`→`>`, `&quot;`→`"`, `&apos;`→`'`.
  Unknown entities are passed through unchanged.
- Pipeline order: `TextNormalizer::normalize()` → `xml_escape()` → chunker.

---

# Text Chunking

**Source:** `reference/edge-tts/src/edge_tts/communicate.py` (`split_text_by_byte_length`, helpers)

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

`ValueError` is raised if `byte_length ≤ 0` or if a valid split cannot be
determined.

**Important:** Chunking operates on the **XML-escaped** text, not the raw text.
Characters like `<`, `>`, `&`, `"`, `'` are escaped before chunking.

**Reference divergence note:** The Python `_find_safe_utf8_split_point` function
receives the entire remaining byte string rather than just the first `byte_length`
bytes, so for valid UTF-8 input it would return `len(remaining)` (splitting the
entire remainder as one chunk instead of hard-splitting at the byte limit).  In
practice this path is never reached for normal text since there are always spaces
within any 4096-byte window.  The C++ `serialization::TextChunker` corrects this
by walking back from the limit position.

**C++ implementation:** `serialization::TextChunker` (`TextChunker.hpp` /
`TextChunker.cpp`) implements the complete reference algorithm:
- `TextChunkerOptions::max_chunk_size = 4096` (reference default)
- `TextChunkerOptions::size_after_xml_escape = true` (reference behavior)
- `TextChunkerOptions::prefer_sentence_boundary = true` (newline preference)
- `TextChunkerOptions::prefer_word_boundary = true` (space fallback)
- Returns XML-escaped, stripped chunks ready for SSML `<prosody>` embedding.
- Propagates UTF-8 validation errors from `TextNormalizer`.

The older `core::TextChunker` performs UTF-8-safe byte splitting only and does
**not** normalize, escape, or protect entities.  The communication layer must
use `serialization::TextChunker`.

**Match exactly:** Yes.

---

# SSML Generation

**Source:** `reference/edge-tts/src/edge_tts/communicate.py` (`mkssml`, `ssml_headers_plus_data`)

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
`Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)` — generated
via `time.strftime("%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)", time.gmtime())`.

**Request ID:** UUID v4 without dashes (`uuid.uuid4().hex`).

**Match exactly:** Yes.

---

# WebSocket Protocol

**Source:** `reference/edge-tts/src/edge_tts/communicate.py` (`__stream`), `reference/edge-tts/src/edge_tts/constants.py`

**WebSocket URL:**
```
wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
  ?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4
  &ConnectionId=<uuid_no_dashes>
  &Sec-MS-GEC=<sha256_token>
  &Sec-MS-GEC-Version=1-143.0.3650.75
```

**WebSocket request headers** (`WSS_HEADERS` merged with `BASE_HEADERS`):

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
5. Connection is closed (or can be reused — the Python code reopens per chunk).

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
is no `--format` CLI flag and no `outputFormat` constructor parameter in the
Python reference.  The C++ `OutputFormat` type enforces the same restriction:
`OutputFormat::from_string()` rejects any value not in the known supported set,
and `OutputFormat::default_format()` returns the one format the Python reference
uses.  See `core::OutputFormat` in `docs/MODULES.md`.

Where `wd = "true"` and `sq = "false"` when `boundary == "WordBoundary"`, and
vice versa for `SentenceBoundary`.

**Incoming text frames:**

| `Path` header | Action |
|---------------|--------|
| `audio.metadata` | Parse JSON body; yield `WordBoundary` or `SentenceBoundary` chunk |
| `turn.end` | Compute offset compensation from audio bytes; break loop |
| `response` | Silently ignored |
| `turn.start` | Silently ignored |
| anything else | Raise `UnknownResponse` |

**Text frame parsing:** `encoded_data.find(b"\r\n\r\n")` locates the header/body
boundary; headers are split on `\r\n` then on the first `:`.

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
  silently skipped (stream termination signal); any other value raises `UnexpectedResponse`.

**Python header-parsing quirk:** `get_headers_and_data(data, HL)` includes the
2-byte prefix in `data[:HL]`, so the first header line has garbage prefix bytes.
The service always places a "don't care" header (e.g., `X-RequestId`) first, so
`Path` and `Content-Type` on subsequent lines parse correctly. The C++ implementation
parses headers from byte 2 only — all headers parse correctly regardless of order.

**C++ implementation:** `communication::EdgeProtocol::parse_incoming(WebSocketMessage)`
→ `Result<vector<IncomingMessage>>`. Defined in `src/communication/EdgeProtocolIncoming.cpp`.
Types: `WebSocketMessage` (in/out), `IncomingMessage` + `IncomingMessageKind` (parsed result).

**Match exactly:** Yes for all documented cases; see PROTOCOL_NOTES.md for the
binary frame format analysis and binary frame validation table.

---

# EdgeTokenProvider — Sec-MS-GEC Generation

**Source:** `reference/edge-tts/src/edge_tts/drm.py` (`DRM.generate_sec_ms_gec()`)

**C++ implementation:** `communication::EdgeTokenProvider`. SHA-256 helper:
`common::sha256_hex_upper`.

**No wall-clock dependency in tests:** `EdgeTokenProvider` accepts `const IClock&`,
allowing `common::FixedClock` for deterministic tests.

**Match exactly:** Yes — algorithm step-by-step identical to Python reference.
Deterministic test vectors prove compatibility (see `PROTOCOL_NOTES.md` —
Sec-MS-GEC Token Generation).

---

# DRM / Sec-MS-GEC

**Source:** `reference/edge-tts/src/edge_tts/drm.py`, `reference/edge-tts/src/edge_tts/constants.py`

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

**MUID cookie:** `muid=<secrets.token_hex(16).upper()>;` — a new random 16-byte
hex token is generated per request.  Added to all voice-list and WebSocket
requests via `DRM.headers_with_muid()`.

**Clock skew correction:** The `DRM.clock_skew_seconds` class variable
accumulates offset corrections.  It starts at `0.0`.  When a `403` response is
received, `handle_client_response_error()` parses the RFC 2616 `Date` response
header and adjusts skew as `server_date - client_date`.

**Ambiguity:** The `ticks` multiplication uses Python float arithmetic
(`ticks *= S_TO_NS / 100` where `S_TO_NS = 1e9`).  The C++ implementation must
produce bit-identical token strings.  Use 64-bit double arithmetic and format
with `%.0f` equivalent to match Python's `f"{ticks:.0f}"`.

**Match exactly:** Yes — token must match the Python output exactly.

---

# Stream Chunk Types

**Source:** `reference/edge-tts/src/edge_tts/typing.py`, `reference/edge-tts/src/edge_tts/communicate.py`

Three chunk types flow from `Communicate.stream()`:

| `type` field | Additional fields | Source |
|--------------|-------------------|--------|
| `"audio"` | `data: bytes` (raw MP3 frames) | Binary WebSocket frame |
| `"WordBoundary"` | `offset: float`, `duration: float`, `text: str` | `audio.metadata` JSON |
| `"SentenceBoundary"` | `offset: float`, `duration: float`, `text: str` | `audio.metadata` JSON |

**Offset and duration units:** 100-nanosecond ticks.
`1 second = 10,000,000 ticks` (constant `TICKS_PER_SECOND = 10_000_000`).

**Tick-to-microsecond conversion** (as used by Python SubMaker):
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

**`text` field:** XML-unescaped (`xml.sax.saxutils.unescape`) from the JSON
`Data.text.Text` field.

**`stream()` is single-use:** calling it twice raises `RuntimeError`.

**C++ single-use behavior (`api::Communicate`):** `stream_sync()` and `save()`
are both single-use. Calling either a second time (or calling one after the
other) returns `ErrorCode::invalid_state` — matching the Python `RuntimeError`.
`save()` calls the synthesis pipeline internally, so both methods consume the
stream.

**C++ chunk types** (`include/edge_tts/core/Chunk.hpp`):

| Python dict | C++ type | Key fields |
|-------------|----------|------------|
| `{"type": "audio", "data": bytes}` | `AudioChunk` | `data: vector<byte>` |
| `{"type": "WordBoundary", ...}` | `BoundaryChunk` with `type = BoundaryEventType::WordBoundary` | `text`, `offset_ticks`, `duration_ticks` |
| `{"type": "SentenceBoundary", ...}` | `BoundaryChunk` with `type = BoundaryEventType::SentenceBoundary` | `text`, `offset_ticks`, `duration_ticks` |

`TtsChunk = std::variant<AudioChunk, BoundaryChunk>`.  Use `is_audio()` and
`is_boundary()` predicates to distinguish.  Zero-duration boundary chunks
are valid (some sentence boundaries have `duration = 0`).

**Match exactly:** Yes.

---

# SubMaker — Boundary Event Accumulation

**Source:** `reference/edge-tts/src/edge_tts/submaker.py`

**C++ implementation:** `subtitles::SubMaker` (`SubMaker.hpp` / `SubMaker.cpp`).

## Public methods

| Python | C++ | Notes |
|--------|-----|-------|
| `feed(msg)` | `feed(BoundaryChunk&)→Result<void>` | Appends a cue; enforces type consistency |
| `get_srt()` | `to_srt()→Result<string>` | Delegates to `SrtComposer`; does not reset state |
| `cues` (attribute) | `cues()→vector<SubtitleCue>` | Returns copy |
| — | `clear()` | Resets cues and type lock (no Python equivalent) |

## Type enforcement

Python stores `self.type` (first boundary type seen) and raises `ValueError` on
mismatch. C++ returns `ErrorCode::invalid_argument` on mismatch. Both accept
`WordBoundary` and `SentenceBoundary`, but all feeds to one `SubMaker` instance
must use the same type.

## Cue time calculation

```python
start = timedelta(microseconds=msg["offset"] / 10)
end   = timedelta(microseconds=(msg["offset"] + msg["duration"]) / 10)
```

C++ equivalent:
```cpp
start = SubtitleTime::from_edge_ticks(boundary.offset_ticks)
end   = SubtitleTime::from_edge_ticks(boundary.offset_ticks + boundary.duration_ticks)
```

## Text storage

`boundary.text` is stored verbatim. `MetadataJsonParser` has already applied
`xml_unescape()` (reference: `unescape()` in `communicate.py`). No text
transformation happens in `SubMaker`.

## State after `to_srt()`

`get_srt()` does not modify state in Python — calling it multiple times returns
the same output, and `feed()` can continue adding cues afterward. C++ `to_srt()`
and `feed()` match this behavior.

## Zero-duration cues

A cue with `duration_ticks == 0` has `start == end`. `SubMaker::feed()` accepts
it (creating the cue), but `SrtComposer` skips it in SRT output because
`start >= end`.

**Match exactly:** Yes for all documented behaviors.

---

# SubtitleTime — Edge Tick Conversion

**Sources:** `reference/edge-tts/src/edge_tts/submaker.py`,
`reference/edge-tts/src/edge_tts/srt_composer.py`

**C++ implementation:** `subtitles::SubtitleTime` (`SubtitleTime.hpp` / `SubtitleTime.cpp`).

## Tick-to-millisecond conversion

Python (`submaker.py`):
```python
start = timedelta(microseconds=msg["offset"] / 10)
```
- `msg["offset"] / 10` — Python 3 **float** division gives microseconds.
- `timedelta(microseconds=float_val)` stores the nearest integer microsecond
  using **banker's rounding** (round-half-to-even) for the fractional part.
- The SRT composer then applies `timedelta.microseconds // 1000` (floor division)
  to get milliseconds.

C++ (`SubtitleTime::from_edge_ticks(ticks)`):
```cpp
milliseconds = ticks / 10'000  // integer truncation
```
This is equivalent to `ticks // 10 // 1000` and matches Python for all values
where the sub-microsecond fractional part does not round the microsecond count
across a millisecond boundary.  A ±1 ms difference can occur only when
`ticks % 10'000 ≥ 9'995` — extremely rare in practice.  The C++ integer
truncation is documented behavior; callers should not rely on sub-millisecond
precision.

**Negative ticks:** `from_edge_ticks` returns `ErrorCode::invalid_argument`.
The Python reference allows negative `timedelta` values but the SRT composer
always skips subtitles whose start time is negative.

## SRT timestamp format

Source: `srt_composer.timedelta_to_srt_timestamp()`

```
HH:MM:SS,mmm
```

- **Comma** separator between seconds and milliseconds (not a dot).
- Each component is zero-padded: HH minimum 2 digits, MM exactly 2, SS exactly 2,
  mmm exactly 3.
- Hours are **not** capped at 99 — values ≥ 100 hours expand the hours field.

Doctest from reference:
```python
>>> timedelta_to_srt_timestamp(timedelta(hours=1, minutes=23, seconds=4))
'01:23:04,000'
```

**Match exactly:** Yes for all tick values where integer truncation equals Python's
float-division + banker's-rounding chain (i.e. all practical values).

---

# Subtitles

**Sources:** `reference/edge-tts/src/edge_tts/submaker.py`, `reference/edge-tts/src/edge_tts/srt_composer.py`

**`SubMaker.feed(msg)`:**
- Accepts only `WordBoundary` or `SentenceBoundary` chunks; others raise `ValueError`.
- All chunks in one session must share the same type; mixing types raises `ValueError`.
- Converts ticks to `timedelta` via `timedelta(microseconds=offset / 10)`.
  Division by 10 converts 100 ns ticks → microseconds.
- End time = `timedelta(microseconds=(offset + duration) / 10)`.
- Appends a `Subtitle(index, start, end, content)` to the internal list.

**SRT format produced by `srt_composer.compose()`:**
```
{index}
{HH:MM:SS,mmm} --> {HH:MM:SS,mmm}
{content}

```

- Index is 1-based and recomputed by `sort_and_reindex()` (sorted by start time).
- Subtitles with empty content, negative start, or start ≥ end are **skipped**.
- Timestamp: `HH:MM:SS,mmm` (comma separator between seconds and milliseconds).
- Each block ends with two newlines.

**Match exactly:** Yes — same skipping rules, same timestamp format.

---

# Playback

**Source:** `reference/edge-tts/src/edge_playback/__main__.py`

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

**Match exactly:** Yes for Linux/macOS (mpv + temp files).  Windows playback is
a best-effort port; the `--mpv` flag must be supported.

---

# Proxy Behavior

**Source:** `reference/edge-tts/src/edge_tts/communicate.py`, `reference/edge-tts/src/edge_tts/voices.py`

A proxy URL string is accepted by both `Communicate` (for WebSocket) and
`list_voices()` (for HTTPS).  The value is passed directly to `aiohttp`.

Proxy support is not validated or parsed by the Python library; any string
(or `None`) is accepted.

**Match exactly:** Yes — proxy must be forwarded to the HTTP/WebSocket client
without modification.

---

# Retry / Clock Skew Behavior

**Sources:** `reference/edge-tts/src/edge_tts/communicate.py` (`stream()`), `reference/edge-tts/src/edge_tts/drm.py`, `reference/edge-tts/src/edge_tts/voices.py`

**Trigger condition:** HTTP 403 response from the service.

**Retry flow (both WebSocket and voice list):**

1. On `403`, call `DRM.handle_client_response_error(e)`.
2. That function reads the `Date` response header (RFC 2616 format), parses it,
   computes `server_date - client_date`, and adds the difference to
   `DRM.clock_skew_seconds`.
3. If the `Date` header is absent or unparseable, raise `SkewAdjustmentError`.
4. Immediately retry the **same request** once with a freshly generated
   `Sec-MS-GEC` token (incorporating the updated clock skew).

**No exponential backoff:** exactly one retry per 403.  Any subsequent failure
is propagated.

**Clock skew is global state:** `DRM.clock_skew_seconds` is a class variable
shared across all `Communicate` and `list_voices` calls in the same process.

**C++ implementation status:** Implemented.

- `WebSocketClient::connect()` maps HTTP 403 → `ErrorCode::drm_error` and stores the
  `Date` response header (from `ix::WebSocketInitResult::headers`) as `error.context()`.
- `SynthesisSession` retry path: if `should_retry()` returns true, calls
  `parse_http_date(error.context())`, computes
  `skew = server_time - (client_now + existing_skew)`, then calls
  `token_provider_.adjust_clock_skew(skew)` before retrying with a new ConnectionId
  and freshly computed `Sec-MS-GEC`.
- If the Date header is absent or malformed, skew adjustment is skipped (no error raised —
  divergence from Python's `SkewAdjustmentError`; the retry still proceeds).
- `parse_http_date()` is in `communication/HttpDate.hpp`; format:
  `"Wkd, DD Mon YYYY HH:MM:SS GMT"` (reference: `drm.py DRM.parse_rfc2616_date()`).
- `EdgeTokenProvider::clock_skew_seconds()` is per-instance (injectable for tests),
  not global process state as in Python.

---

# Error Behavior

**Source:** `reference/edge-tts/src/edge_tts/exceptions.py`

```
EdgeTTSException (base)
├── UnknownResponse    — unknown metadata type or unknown WebSocket path
├── UnexpectedResponse — unexpected but known-format response (protocol violation)
├── NoAudioReceived    — stream completed without yielding any audio chunks
├── WebSocketError     — WebSocket-level error (aiohttp WSMsgType.ERROR)
└── SkewAdjustmentError — 403 handling failed (no/unparseable Date header)
```

**C++ mapping:**

| Python exception | C++ equivalent |
|------------------|---------------|
| `UnknownResponse` | `common::ProtocolError` subtype or new `UnknownResponseError` |
| `UnexpectedResponse` | `common::ProtocolError` subtype or new `UnexpectedResponseError` |
| `NoAudioReceived` | New `NoAudioReceivedError` in `common::` |
| `WebSocketError` | `common::NetworkError` subtype |
| `SkewAdjustmentError` | New `SkewAdjustmentError` in `common::` |

**Match exactly:** Yes — same triggering conditions, C++ error types may differ
in name but must carry the same semantics.

---

# Compatibility Targets

**Source:** `reference/edge-tts/src/edge_tts/version.py`, `reference/edge-tts/setup.cfg`

| Property | Value |
|----------|-------|
| Reference version | `7.2.8` |
| Python requirement | `>=3.7` |
| Chromium version string | `143.0.3650.75` |
| `SEC_MS_GEC_VERSION` header | `1-143.0.3650.75` |
| `TRUSTED_CLIENT_TOKEN` | `6A5AA1D4EAFF4E9FB37E23D68491D6F4` |
| Audio output format | `audio-24khz-48kbitrate-mono-mp3` |
| Audio bitrate (for timing) | 48,000 bps |
| Tick resolution | 100 nanoseconds (10,000,000 ticks/second) |

The Chromium version and trusted token are likely to change when Microsoft
updates the Edge TTS service.  The C++ implementation should keep these as
named constants, not hard-coded literals scattered through the code.

**Match exactly:** Yes for all constants.

---

# Ambiguities / Requires Live Verification

The following behaviors are inferred from the code but have not been verified
against the live service:

| # | Topic | Observation | Open Question |
|---|-------|-------------|---------------|
| 1 | Voice name regex | Python validates with `\(.+,.+\)` (no space required after comma) but always generates a space: `(en-US, Emma…)`. | Does the service accept the no-space form? |
| 2 | `speech.config` per chunk | A new WebSocket connection is opened per text chunk; `speech.config` is re-sent each time. | Can connections be reused across chunks? |
| 3 | Offset compensation integer overflow | Python comment says Microsoft's metadata offsets overflow on long texts. The CBR-byte approach is the fix. | Is 64-bit integer arithmetic sufficient for arbitrarily long audio in C++? |
| 4 | `turn.end` binary | The code expects `turn.end` as a text frame and processes offset compensation there. | Does the service ever send `turn.end` as binary? |
| 5 | Empty binary with no Content-Type | The code silently skips zero-length binary frames with no Content-Type. | Is this a documented termination signal or just observed empirically? |
| 6 | `SessionEnd` metadata | `SessionEnd` metadata type is silently skipped in `__parse_metadata`. | Are there other metadata types not yet observed? |
| 7 | Retry count | Only one retry per 403 is implemented. | Does the service ever require more than one clock-skew correction per session? |
| 8 | `--write-media -` | Writing MP3 to stdout is supported (the `-` flag). | How is this tested without a live service? |
| 9 | Determinism | The 26-parallel-process test (`001-long-text.sh`) compares SRT outputs for equality. | Are SRT outputs truly deterministic across connections, or is there sentence-boundary jitter? |
| 10 | DRM float precision | Token generation multiplies by `1e9 / 100` using Python float. | Does IEEE 754 double produce the same `.0f`-formatted integer as the service expects on all platforms? |
