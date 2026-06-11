# Protocol Notes

Reference for time, epoch, timestamp, and wire-format details for the
Edge TTS service.  These notes inform implementation of the serialization
and communication layers.

---

## Parser Compatibility Policy

The Edge TTS WebSocket protocol is reverse-engineered and is not formally
documented by Microsoft.  The service may change message shapes, add new field
types, or introduce new event categories without notice.

### General principles

| Situation | Parser behaviour | Rationale |
|-----------|-----------------|-----------|
| **Unknown text frame Path** (not `turn.start`, `response`, `audio.metadata`, `turn.end`) | Hard error (`protocol_error`) | A completely unknown path indicates a fundamental protocol change that the caller must know about. |
| **Unknown binary frame Path** (not `audio`) | Hard error (`protocol_error`) | Binary frames carry audio payload; a wrong path indicates unexpected data. |
| **Unknown `audio.metadata` Type** (not `WordBoundary`, `SentenceBoundary`, `SessionEnd`) | **Silently skipped** | The service can add new event types (e.g. `VisemeBoundary`) without breaking subtitle extraction; unknown entries are dropped and synthesis continues. |
| **`SessionEnd` metadata type** | Silently skipped | Normal end-of-stream signal; no error needed. |
| **Extra JSON fields** in metadata or voice entries | Silently ignored | `nlohmann/json` ignores unknown keys by default; forward-compatible by design. |
| **Missing required JSON fields** | Hard error (`parse_error`) | Required fields are load-bearing; their absence means we cannot produce a valid result. |
| **Malformed frame** (bad separator, bad binary header, invalid JSON) | Hard error (`protocol_error` or `parse_error`) | Corrupt data must never be accepted silently. Error messages include truncated raw context for diagnostics. |
| **Unknown `VoiceTag` fields** or unknown top-level voice entry fields | Silently ignored | The voice schema is stable for required fields; new optional fields from the service are irrelevant to C++ consumers. |

### Safety net for empty metadata

`MetadataJsonParser` may return an empty vector when every entry is either
skipped (unknown type) or `SessionEnd`.  `EdgeProtocolIncoming` treats an
empty result as `protocol_error("No WordBoundary metadata found")` — the
caller never silently receives an empty synthesis.

### Error context rule

All parser errors that mention a specific protocol value (path name, unknown
type, bad header line) must include that value in `Error::context()` so
operators can diagnose service changes without additional logging.  Large
payloads (e.g. audio bytes) are **never** included in error context.

---

## WebSocket Transport

**C++ implementation:** `communication::WebSocketClient` (`WebSocketClient.hpp` / `WebSocketClient.cpp`)

### URL construction

The full WebSocket URL is assembled by `SynthesisSession` before calling `connect()`:

```
wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
  ?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4
  &ConnectionId=<uuid_without_hyphens>
  &Sec-MS-GEC=<sha256_token>
  &Sec-MS-GEC-Version=1-143.0.3650.75
```

- `ConnectionId`: UUID v4 without hyphens (32 lowercase hex chars)
- `Sec-MS-GEC`: SHA-256 of Windows file time rounded to 5 min + trusted token
- `Sec-MS-GEC-Version`: `1-<CHROMIUM_FULL_VERSION>` = `1-143.0.3650.75`

### Upgrade request headers

| Header | Value |
|--------|-------|
| `Pragma` | `no-cache` |
| `Cache-Control` | `no-cache` |
| `Origin` | `chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold` |
| `User-Agent` | `Mozilla/5.0 … Chrome/143.0.0.0 … Edg/143.0.0.0` |
| `Accept-Encoding` | `gzip, deflate, br, zstd` |
| `Accept-Language` | `en-US,en;q=0.9` |
| `Cookie` | `muid=<random_16_byte_hex_uppercase>` |

In C++, these are built by `communication::build_websocket_headers(config, ids)` in
`EdgeRequestHeaders.hpp/.cpp` and passed as `WebSocketClientOptions::extra_headers` by the
`SynthesisSession` caller.

#### MUID generation

`IdGenerator::random_32_hex()` produces 32 lowercase hex chars; the
`make_muid_cookie()` helper (local to `EdgeRequestHeaders.cpp`) uppercases them and produces
the full `"muid=<32 UPPER HEX>;"` string.  One fresh MUID is generated per call.

#### HTTP transport vs. service layer

`IHttpClient::send()` is a pure transport boundary: it returns a successful
`Result<HttpResponse>` for any HTTP status code, including 403, 500, etc.
Transport-level failures only (network unreachable, TLS error, timeout,
unsupported proxy) produce `Result::fail`.

Service-layer code (`VoiceService`) is responsible for mapping HTTP status codes
to application errors:
- HTTP 403 → DRM retry path (compute clock skew, retry once)
- any other non-200 → `ErrorCode::service_error` with status code as context

This separation means `FakeHttpClient` can inject any status code directly
into service tests without the transport layer interfering.

#### Voice-list request headers

The following HTTP headers are sent by `VoiceService::list_voices()`
(see `EdgeServiceConfig::voice_list_headers()`).
Built by `communication::build_voice_list_headers(config, ids)`:

| Header | Value |
|--------|-------|
| `User-Agent` | `Mozilla/5.0 … Chrome/143.0.0.0 … Edg/143.0.0.0` |
| `Accept-Encoding` | `gzip, deflate, br, zstd` |
| `Accept-Language` | `en-US,en;q=0.9` |
| `Accept` | `*/*` |
| `Cookie` | `muid=<random_16_byte_hex_uppercase>` |

#### Voice-list URL: DRM token appended by VoiceService

`EdgeServiceConfig::voices_endpoint` stores only the base URL (without DRM params).
`communication::VoiceService` appends the token params at request time:

```
<voices_endpoint>&Sec-MS-GEC=<sha256_token>&Sec-MS-GEC-Version=<version>
```

The token is generated by `EdgeTokenProvider::sec_ms_gec()` using the same SHA-256
algorithm as the WebSocket URL.  `Sec-MS-GEC-Version` comes from
`EdgeTokenProvider::sec_ms_gec_version()`.

#### Voice-list retry on HTTP 403

**Rules:**
- **One retry** per `list_voices()` call, only on HTTP 403.
- All other HTTP errors propagate immediately.
- Clock skew is adjusted before the retry token is regenerated.

**C++ implementation:** `communication::VoiceService::list_voices()` calls the
private `send_request()` helper (returns the raw `HttpResponse`).  On HTTP 403:

1. Inspect the `Date` response header.
2. Parse it with `parse_http_date()` (same parser as the WebSocket path).
3. If valid: call `EdgeTokenProvider::adjust_clock_skew_from_server_timestamp(server_ts)`,
   which computes `skew = server_ts - (clock_now + current_skew)` and accumulates it.
4. **Fallback:** If `Date` is absent or unparsable, call `adjust_clock_skew(300.0)`.
5. Call `send_request()` once more with the corrected token.

### Timeouts

| Parameter | Default |
|-----------|---------|
| Connect timeout | 10 000 ms |
| Read timeout | 60 000 ms |

### Message receive loop

| Message type | Path / condition | C++ action |
|---|---|---|
| TEXT | `audio.metadata` | Parse JSON → `BoundaryChunk` |
| TEXT | `turn.end` | apply offset compensation; break loop |
| TEXT | `response`, `turn.start` | silently ignored |
| TEXT | anything else | `UnknownResponse` → `protocol_error` |
| BINARY | `Path: audio`, `Content-Type: audio/mpeg`, non-empty | `AudioChunk` |
| BINARY | `Path: audio`, no `Content-Type`, empty body | ignored |
| BINARY | all other conditions | `UnexpectedResponse` → `protocol_error` |
| ERROR | any | `WebSocketError` → `network_error` |

### Retry on HTTP 403 (DRM token rejection)

**Rules:**
- **One retry** per chunk, only on HTTP 403 from the WebSocket upgrade (`ws_connect`).
- All other errors propagate immediately (no retry).
- The clock skew is adjusted from the server's `Date` response header, then the token is regenerated.

**C++ implementation:** `communication::RetryPolicy` + `SynthesisSession` retry path + `EdgeTokenProvider::adjust_clock_skew(double)`.

| Condition | C++ |
|-----------|-----|
| HTTP 403 WebSocket upgrade failure | `ErrorCode::drm_error` from `WebSocketClient::connect()` |
| Date header parsing | `parse_http_date()` in `communication/HttpDate.hpp` |
| Clock skew correction | `EdgeTokenProvider::adjust_clock_skew(seconds)` |
| Retry limit | `RetryPolicy::max_retries` (default 1) |

**Date header extraction:** `ix::WebSocketInitResult::headers` is a `CaseInsensitiveLess` map that includes all HTTP response headers from the upgrade attempt. On HTTP 403, `WebSocketClient::connect()` reads `init.headers["Date"]` and stores it as the `context()` field of the `drm_error`. `SynthesisSession` then parses this with `parse_http_date()`.

**Clock skew formula:**
```
skew = server_time - (client_now + existing_skew)
EdgeTokenProvider::adjust_clock_skew(skew)
// After adjustment: total_skew = server_time - client_now
```

**Fallback behavior:** If the 403 response carries no Date header (context is empty) or the date string is malformed, the retry still proceeds — just without skew correction.

**RFC 2616 date format:** `"Wkd, DD Mon YYYY HH:MM:SS GMT"` (e.g. `"Mon, 15 Jan 2024 08:31:15 GMT"`). Implemented in `communication::parse_http_date()` using Howard Hinnant's Gregorian day-count algorithm. Weekday and timezone fields are accepted but not validated.

### Close behavior

`SynthesisSession::synthesize()` calls `websocket.close()` after the receive loop
regardless of success or error, and before returning any error.

### Proxy

`WebSocketClientOptions::proxy` stores an optional proxy URL. The ixwebsocket synchronous API
has no CONNECT-tunnel proxy support, so if `proxy` is set, `connect()` returns
`ErrorCode::unsupported` before attempting the connection. Proxy is not
functional end-to-end; callers receive a clear error rather than a silent no-op.

---

## Connection and Request IDs

**C++ implementation:** `communication::ConnectionMetadata` struct +
`ConnectionMetadataFactory` class.

### Format

| ID | Location | Format |
|----|----------|--------|
| `connection_id` | `&ConnectionId=` URL query param | UUID v4 without hyphens, 32 lowercase hex chars |
| `request_id` | `X-RequestId:` SSML frame header | UUID v4 without hyphens, 32 lowercase hex chars |

Both use `IdGenerator::uuid_v4_without_hyphens()`.
Two separate calls → always distinct values.

The speech.config frame does NOT carry X-RequestId (only X-Timestamp and Path).

### Lifecycle

One `ConnectionMetadata` is produced per text chunk processed:
- `connection_id` is appended to the WebSocket URL when opening the connection.
- `request_id` is placed in the `X-RequestId` header of the SSML frame sent on that connection.

---

## Service Constants

All Edge TTS service constants live in `communication::EdgeServiceConfig` and
are populated by `default_edge_service_config()` in
`src/communication/EdgeServiceConfig.cpp`.

| Field | Value |
|-------|-------|
| `websocket_endpoint` | `wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4` |
| `voices_endpoint` | `https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4` |
| `trusted_client_token` | `6A5AA1D4EAFF4E9FB37E23D68491D6F4` |
| `sec_ms_gec_version` | `1-143.0.3650.75` |
| `origin` | `chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold` |
| `user_agent` | `Mozilla/5.0 (Windows NT 10.0; Win64; x64) ... Chrome/143.0.0.0 ... Edg/143.0.0.0` |
| `speech_config_path` | `speech.config` |
| `ssml_path` | `ssml` |
| `audio_metadata_path` | `audio.metadata` |
| `turn_end_path` | `turn.end` |

**Case difference:** `WSS_URL` uses `TrustedClientToken` (mixed case) while
`VOICE_LIST` uses `trustedclienttoken` (lower case) — this matches the
reference exactly and must not be normalized.

**Dynamic params appended by callers** (not in config):
- WebSocket: `&ConnectionId=<uuid>&Sec-MS-GEC=<sha256>&Sec-MS-GEC-Version=<ver>`
- Voice list: `&Sec-MS-GEC=<sha256>&Sec-MS-GEC-Version=<ver>`

---

## Sec-MS-GEC Token Generation

**C++ implementation:** `communication::EdgeTokenProvider` (`EdgeTokenProvider.hpp` /
`EdgeTokenProvider.cpp`). SHA-256 helper: `common::sha256_hex_upper` (`Sha256.hpp` /
`Sha256.cpp`).

### Algorithm

Steps:

1. Get `double unix_seconds` from `IClock::now().time_since_epoch()`.
2. `double ticks = unix_seconds + 11644473600.0`
3. `ticks -= std::fmod(ticks, 300.0)` — rounds down to nearest 5-minute boundary.
4. `ticks *= 1e9 / 100.0` — IEEE 754 double arithmetic.
5. `snprintf(buf, "%.0f", ticks)` — integer-format the tick count.
6. Concatenate with `trusted_client_token` from `EdgeServiceConfig`.
7. `common::sha256_hex_upper(str_to_hash)` — SHA-256 (FIPS 180-4), uppercase hex.

### Float arithmetic

The `%.0f` format uses IEEE 754 round-to-nearest-even. After step 3, `ticks` is
an exact multiple of 300 (representable exactly in double). After step 4,
`ticks` may not be exactly representable, but `snprintf` with `%.0f` produces
the correct rounded decimal string in step 5.

**Deterministic test vectors** (tested in `EdgeTokenProviderTests.cpp`):

| Unix timestamp | Expected token (uppercase hex SHA-256) |
|---|---|
| 0 | `7ECB79D14E3AA576D2D79E6D487A1388156D91E614B1BE11C64226A29BC8DD8C` |
| 1000000000 | `6594DCF2D741A251B0EDFB71C0034EBFEBF6D413CC1EA5D1B23E60B118A2F0E1` |
| 1700000000 | `42301B335578FEFDAE2637DED1ABD614505D432559EC08032B82048483726AFF` |

### Bucket boundary

Two timestamps in the same 300-second window produce the same token. The first
unix timestamp that starts a new window is the one where `(unix + WIN_EPOCH) % 300 == 0`.

### Clock skew

On a 403 response, the server's `Date` header is parsed and the skew is
adjusted. In C++, the `IClock` abstraction allows the communication layer to
inject a corrected clock without modifying `EdgeTokenProvider`.

### sec_ms_gec_version

`EdgeTokenProvider::sec_ms_gec_version()` returns
`config.sec_ms_gec_version` verbatim (= `"1-143.0.3650.75"`). No computation.

---

## UTC usage

All timestamps use UTC exclusively. `std::chrono::system_clock` is UTC-based,
so no conversion is needed at the `IClock` level.

---

## Timestamp formats used

### DRM / `Sec-MS-GEC` token

1. Start with current UTC Unix time (float seconds since 1970-01-01 UTC).
2. Add clock skew correction (float seconds, initially 0.0).
3. Add Windows file time epoch offset: **`11644473600` seconds** — the number of
   seconds between 1601-01-01 00:00:00 UTC (Windows epoch) and 1970-01-01
   00:00:00 UTC (Unix epoch).
4. Round **down** to the nearest 5-minute boundary: `ticks -= ticks % 300`.
5. Convert to 100-nanosecond ticks: multiply by `1e9 / 100` using IEEE 754
   double arithmetic.
6. Concatenate the integer-formatted tick count with the trusted client token:
   `"{ticks_decimal}6A5AA1D4EAFF4E9FB37E23D68491D6F4"`.
7. SHA-256 hash, return uppercase hex string.

This entire algorithm belongs in the DRM/token generation code
(**`communication`** layer), NOT in `IClock`.

### WebSocket `X-Timestamp` header

JavaScript-style UTC date string:

```
Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)
```

Format string: `"%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)"`.

A literal `Z` is then appended by the SSML envelope builder. This is documented
as a known Microsoft Edge bug.

This formatting belongs in the serialization/communication layer.

---

## Clock skew

A correction offset accumulates on each 403 response: the `Date` response
header (RFC 2616 format) is parsed and the difference `server_date - client_date`
is applied before the next token generation.

**C++ design note:** `IClock` does not carry a skew field. Instead, the token
generation code holds a mutable skew offset applied to `IClock::now()` before
the epoch conversion. `FixedClock` makes this testable by setting the exact
`time_point` the token generator will observe.

---

## `IClock` contract

| Property | Value |
|----------|-------|
| Return type | `std::chrono::system_clock::time_point` |
| Epoch | 1970-01-01 00:00:00 UTC |
| Granularity | At least microsecond (system_clock resolution is platform-dependent, typically nanosecond) |
| Protocol constants | None — epoch offsets and tick conversions are done at the call site |

---

## SSML document format

The SSML document sent inside the WebSocket `ssml` path frame is a single
UTF-8 line with no inter-element whitespace:

```
<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'><voice name='{full_voice}'><prosody pitch='{pitch}' rate='{rate}' volume='{volume}'>{escaped_text}</prosody></voice></speak>
```

Key invariants:
- All attribute values use **single quotes**.
- `xml:lang` is **hardcoded `en-US`** regardless of voice locale.
- `<voice name>` always uses the **full form**: `Microsoft Server Speech Text to Speech Voice (locale, name)`.  Short-form voice names (`en-US-EmmaMultilingualNeural`) are normalized before embedding.
- Prosody attribute order: **pitch, rate, volume**.
- Text content is **XML-escaped** (`&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`).
- The SSML body does **not** include WebSocket framing or `X-RequestId` / `X-Timestamp` headers — those are added by the frame builder in the communication layer.

**C++ implementation:** `serialization::SsmlBuilder::build(config, raw_text)`:
1. Validates `TtsConfig` via `validate_tts_config()`.
2. Normalizes the voice name to the full form via `normalize_voice_name()`.
3. Normalizes `raw_text` via `TextNormalizer` (UTF-8 validation + control-char replacement).
4. XML-escapes the normalized text via `xml_escape()` exactly once.
5. Assembles the SSML string.

---

## Protocol Text Frame Format

**C++ implementation:** `serialization::ProtocolMessage`, `serialization::ProtocolParser`,
`serialization::ProtocolSerializer`.

### Wire format

```
Name:Value\r\n
Name:Value\r\n
\r\n
{body}
```

- Headers are separated by `\r\n` (CRLF).
- The header/body boundary is `\r\n\r\n` (blank CRLF line).
- Header name and value are separated by the first `:` — no space between name
  and the colon, no space between colon and value.  Values may contain colons
  (e.g. `Content-Type:application/json; charset=utf-8`).
- Body follows immediately after `\r\n\r\n` with no additional prefix.
- LF-only frames (`\n` instead of `\r\n`) are NOT valid — the reference splits
  exclusively on `\r\n`.

### Outgoing frame header sets

**SSML frame** (`EdgeProtocol::build_ssml_frame`):
```
X-RequestId:{uuid_without_dashes}\r\n
Content-Type:application/ssml+xml\r\n
X-Timestamp:{js_date_string}Z\r\n
Path:ssml\r\n
\r\n
{ssml_body}
```

**C++ implementation:** `communication::EdgeProtocol::build_ssml_frame(config, text_chunk, metadata)`.

Key invariants:
- `X-RequestId` uses `metadata.request_id` (32-char lowercase hex, no hyphens).
- `X-Timestamp` has a trailing `Z` — documented as a Microsoft Edge bug in the source.
- Header order: `X-RequestId`, `Content-Type`, `X-Timestamp`, `Path`.
- `text_chunk` MUST be XML-escaped (output of `serialization::TextChunker`).
  `SsmlBuilder::build_from_escaped_text` embeds it verbatim — no second escape.
  Passing raw text will embed literal XML special characters and produce malformed SSML.
- Config errors from `SsmlBuilder` (invalid rate/volume/pitch/voice) propagate as
  `Result` failures — no silent truncation.
- No chunking logic — callers are responsible for splitting text before calling.

**XML-escaping contract across the pipeline:**

```
api::SpeechSynthesizer::run_pipeline()
  → serialization::TextChunker::chunk()     ← normalize + xml_escape + split
      → text_chunks (XML-escaped strings)
  → SynthesisSession::synthesize(tts_config, text_chunks)
      → EdgeProtocol::build_ssml_frame(config, text_chunk, metadata)
          → SsmlBuilder::build_from_escaped_text(config, text_chunk)
              → embeds text_chunk verbatim (no second escape)
```

`SsmlBuilder` provides two entry points:
- `build(config, raw_text)` — normalizes + XML-escapes + assembles.
  Use for user-supplied raw text (e.g. direct `SsmlBuilder` callers).
- `build_from_escaped_text(config, escaped_text)` — assembles from already-escaped text.
  Used by `EdgeProtocol::build_ssml_frame` to avoid double-escaping chunked input.

**Speech config frame** (`EdgeProtocol::build_speech_config_frame`):
```
X-Timestamp:{js_date_string}\r\n
Content-Type:application/json; charset=utf-8\r\n
Path:speech.config\r\n
\r\n
{"context":{"synthesis":{"audio":{"metadataoptions":{"sentenceBoundaryEnabled":"{sq}","wordBoundaryEnabled":"{wd}"},"outputFormat":"{format}"}}}}\r\n
```

**C++ implementation:** `communication::EdgeProtocol::build_speech_config_frame(config, metadata)`.

Key invariants:
- NO `Z` suffix on the timestamp in `speech.config` — only SSML has it.
- NO `X-RequestId` header — speech.config carries only `X-Timestamp`, `Content-Type`, and `Path`.
- `sentenceBoundaryEnabled` and `wordBoundaryEnabled` values are **JSON strings** (`"true"`/`"false"`), not JSON booleans.
- Boundary logic:
  - `WordBoundary` → `wordBoundaryEnabled:"true"`, `sentenceBoundaryEnabled:"false"`
  - `SentenceBoundary` (default) → `sentenceBoundaryEnabled:"true"`, `wordBoundaryEnabled:"false"`
- Output format is taken from `TtsConfig::output_format.value()` (not hardcoded).
- Body includes a trailing `\r\n`.
- `EdgeProtocol` takes a `common::IClock&` for testable timestamps; use `FixedClock` in tests.
- `ConnectionMetadata` is accepted for API consistency but not used (speech.config has no per-request ID).

### Incoming frame paths

| `Path` header | Type | Handling |
|---------------|------|----------|
| `audio.metadata` | text | Parse JSON body as boundary metadata |
| `turn.end` | text | Turn-end marker; break loop |
| `response` | text | Silently ignored |
| `turn.start` | text | Silently ignored |
| `audio` | binary | Parse audio payload |
| anything else | — | `UnknownResponse` (protocol_error) |

**C++ implementation:** `communication::EdgeProtocol::parse_incoming(message)` →
`Result<vector<IncomingMessage>>`.

### Incoming text frame parsing

Parsing splits on `\r\n\r\n` to separate headers from body:
- `ProtocolParser` uses `pos + 4` (full `\r\n\r\n` skip) to produce a clean body.
- Headers are returned as `vector<pair<string,string>>` (preserves duplicates and ordering).

### Incoming binary frame format

```
[HL_MSB, HL_LSB, header_content (HL-2 bytes), \r\n, body_bytes...]
```

- `HL` = big-endian uint16 from bytes 0–1 = total size of prefix + header content
  (i.e., HL = 2 + len(header_content_without_trailing_CRLF))
- Header content at bytes `[2 .. HL)` — parsed without the 2-byte length prefix
- `\r\n` separator at bytes `[HL .. HL+2)`
- Body at bytes `[HL+2 ..)`

The C++ implementation parses headers from byte 2 onwards (skipping the 2-byte length
prefix), so all headers parse correctly regardless of order.

**Binary frame validation:**

Rows marked **(stricter)** add deterministic rejection of malformed frames that
would otherwise be silently ignored or produce garbled output.

| Condition | Reference behavior | C++ result | Notes |
|-----------|-------------------|------------|-------|
| `len(data) < 2` | `UnexpectedResponse` | `protocol_error` | same as reference |
| `HL < 2` | `ValueError` in header split | `protocol_error` | **(stricter)** — minimum is 2 (2-byte prefix) |
| `HL > len(data)` | `UnexpectedResponse` | `protocol_error` | same as reference |
| `HL + 2 > len(data)` | yields empty body (no check) | `protocol_error` | **(stricter)** — separator must be present |
| `data[HL..HL+2) != \r\n` | not checked (bytes not verified) | `protocol_error` | **(stricter)** — separator bytes must be correct |
| `Path != "audio"` | `UnexpectedResponse` | `protocol_error` | same as reference |
| `Content-Type` not in `{audio/mpeg, absent}` | `UnexpectedResponse` | `protocol_error` | same as reference |
| `Content-Type` absent + empty body | `continue` (ignored) | `IncomingMessageKind::ignored` | same as reference |
| `Content-Type` absent + non-empty body | `UnexpectedResponse` | `protocol_error` | same as reference |
| `Content-Type: audio/mpeg` + empty body | `UnexpectedResponse` | `protocol_error` | same as reference |
| `Content-Type: audio/mpeg` + non-empty body | `yield audio` | `IncomingMessageKind::audio` | same as reference |

---

## SynthesisSession Lifecycle

**C++ implementation:** `communication::SynthesisSession::synthesize(tts_config, text_chunks)`

### Per-chunk sequence

```
1. metadata_factory.create_for_request()   → connection_id + request_id
2. token_provider.sec_ms_gec()             → Sec-MS-GEC token
3. url = endpoint + &ConnectionId=<id>
               + &Sec-MS-GEC=<token>
               + &Sec-MS-GEC-Version=<ver>
4. websocket.connect(url)
5. websocket.send_text(build_speech_config_frame(tts_config, metadata))
6. websocket.send_text(build_ssml_frame(tts_config, text, metadata))
7. receive loop:
     audio    → accumulate AudioChunk
     boundary → accumulate BoundaryChunk
     turn_end → break
     ignored  → continue
     error    → fall through to close + return error
8. if no audio received → service_error  (reference: NoAudioReceived)
9. websocket.close()   ← always, on success AND error (context manager)
```

### Multi-chunk behavior

For each chunk, the full lifecycle in steps 1–9 is repeated with a **new** WebSocket connection.

### Error propagation

| Step that fails | C++ ErrorCode |
|-----------------|---------------|
| `sec_ms_gec()` | `protocol_error` |
| `connect()` | `network_error` |
| `send_text()` | `network_error` |
| `receive()` | `network_error` |
| `parse_incoming()` | `protocol_error` |
| no audio received | `service_error` |

On any error after `connect()`, `close()` is always called before the error is
returned. The close result is silently discarded.

---

### Metadata all-SessionEnd deviation

`MetadataJsonParser` returns an empty vector when the Metadata array is empty or
all-`SessionEnd`. `parse_incoming` treats an empty result as `protocol_error`
(`"No WordBoundary metadata found"`).

---

## Metadata Frame JSON Format

**C++ implementation:** `serialization::MetadataJsonParser`

### JSON shape

```json
{
  "Metadata": [
    {
      "Type": "WordBoundary" | "SentenceBoundary" | "SessionEnd",
      "Data": {
        "Offset":   <integer, 100 ns ticks>,
        "Duration": <integer, 100 ns ticks>,
        "text": {
          "Text": "<xml-escaped string>",
          ...
        }
      }
    }
  ]
}
```

### Field access path

| C++ field | JSON path |
|-----------|-----------|
| `BoundaryChunk::type` | `Metadata[i].Type` → enum |
| `BoundaryChunk::offset_ticks` | `Metadata[i].Data.Offset` |
| `BoundaryChunk::duration_ticks` | `Metadata[i].Data.Duration` |
| `BoundaryChunk::text` | `xml_unescape(Metadata[i].Data.text.Text)` |

Note: `text` (lowercase) contains `Text` (uppercase) — both keys are case-sensitive.

### Event type handling

| Type | Handling |
|------|----------|
| `"WordBoundary"` | Parsed → `BoundaryEventType::WordBoundary` |
| `"SentenceBoundary"` | Parsed → `BoundaryEventType::SentenceBoundary` |
| `"SessionEnd"` | Silently skipped (`continue` in reference) |
| anything else | **Silently skipped** — see Parser Compatibility Policy |

Unknown types are skipped at the `MetadataJsonParser` level. `EdgeProtocol`
applies a safety net: if the resulting boundary list is empty it returns
`protocol_error("No WordBoundary metadata found")`. This enables forward
compatibility when the service introduces new event types (e.g. `VisemeBoundary`)
without breaking synthesis.

### Offset compensation

**Status: implemented in `communication::SynthesisSession`.**

The service reports each chunk's boundary `Offset` values relative to that
chunk's own audio start (i.e. they reset to 0 at the start of each
`SynthesisSession::synthesize` call for multi-chunk text).  `SynthesisSession`
converts them to absolute offsets across the full synthesis by adding an
`offset_compensation` value derived purely from boundary metadata:

```
// cumulative subtitle end-tick from all completed chunks:
cumulative_subtitle_ticks = 0   // before first chunk

// At the start of each text chunk:
offset_compensation = cumulative_subtitle_ticks

// For each BoundaryChunk received (raw = not yet compensated):
raw_end = bc.offset_ticks + bc.duration_ticks
if raw_end > chunk_boundary_end: chunk_boundary_end = raw_end
bc.offset_ticks += offset_compensation    // yields absolute offset

// After turn.end (chunk complete):
cumulative_subtitle_ticks += chunk_boundary_end
```

`chunk_boundary_end` equals `max(offset_ticks + duration_ticks)` over all
boundary events in the chunk, or 0 if none were received.  All arithmetic
uses 64-bit integers (`int64_t`).  `duration_ticks` is never modified.

The metadata-derived formula above is format-independent and produces correct
results regardless of audio bitrate.

### XML unescape

The `"Text"` field value is XML-unescaped via `serialization::xml_unescape()`:
- `&amp;` → `&`
- `&lt;` → `<`
- `&gt;` → `>`
- `&quot;` → `"`
- `&apos;` → `'`

---

## Timing constants

These belong in the serialization/communication layer, not in `IClock`.

| Constant | Value |
|----------|-------|
| Windows file time epoch offset | `11644473600` seconds |
| Nanoseconds per second | `1e9` |
| Tick duration | 100 nanoseconds |
| Ticks per second | `10,000,000` |
| Token round-down boundary | 300 seconds (5 minutes) |
