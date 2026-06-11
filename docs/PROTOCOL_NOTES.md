# Protocol Notes

Reference for time, epoch, and timestamp details observed in the Python
`edge-tts` v7.2.8 source.  These notes inform implementation of the
serialization and communication layers.

All time-related facts here are derived from direct inspection of the Python
reference:

- `reference/edge-tts/src/edge_tts/drm.py`
- `reference/edge-tts/src/edge_tts/communicate.py`
- `reference/edge-tts/src/edge_tts/constants.py`

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
| **`SessionEnd` metadata type** | Silently skipped | Documented reference behaviour (`continue` in `communicate.py`). |
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

**Source:** `communicate.py Communicate.__stream()`, `constants.py WSS_HEADERS`

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

- Base URL: `constants.py WSS_URL`
- `ConnectionId`: `connect_id()` — UUID v4 without hyphens (32 lowercase hex chars)
- `Sec-MS-GEC`: `DRM.generate_sec_ms_gec()` — SHA-256 of Windows file time rounded to 5 min + token
- `Sec-MS-GEC-Version`: `constants.py SEC_MS_GEC_VERSION` = `1-<CHROMIUM_FULL_VERSION>`

### Upgrade request headers

The following HTTP headers are sent on the WebSocket upgrade request
(`WSS_HEADERS` in `constants.py`, with `Cookie` added by `DRM.headers_with_muid()`):

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

Python reference: `DRM.generate_muid() = secrets.token_hex(16).upper()` (32 uppercase hex chars)
followed by `Cookie: f"muid={muid};"`.

C++ implementation: `IdGenerator::random_32_hex()` produces 32 lowercase hex chars; the
`make_muid_cookie()` helper (local to `EdgeRequestHeaders.cpp`) uppercases them and produces
the full `"muid=<32 UPPER HEX>;"` string.  One fresh MUID is generated per call to either
builder — matching Python's per-request cookie generation.

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
(`VOICE_HEADERS` in `constants.py`, with `Cookie` added by `DRM.headers_with_muid()`).
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

**Source:** `voices.py list_voices()` try/except block

```python
try:
    return await session.get(url, headers=VOICE_HEADERS, ssl=True)
except aiohttp.ClientResponseError as e:
    if e.status != 403:
        raise
    DRM.handle_client_response_error(e)   # adjust clock_skew_seconds from Date header
    return await session.get(url, headers=VOICE_HEADERS, ssl=True)
```

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

| Parameter | Python default | C++ default |
|-----------|---------------|-------------|
| Connect timeout | `sock_connect=10` s | `connect_timeout = 10 000 ms` |
| Read timeout | `sock_read=60` s | `read_timeout = 60 000 ms` |

### Message receive loop

Reference `__stream()` receive loop behavior:

| Message type | Path / condition | C++ action |
|---|---|---|
| TEXT | `audio.metadata` | Parse JSON → `BoundaryChunk` |
| TEXT | `turn.end` | `__compensate_offset()`; break loop |
| TEXT | `response`, `turn.start` | silently ignored |
| TEXT | anything else | `UnknownResponse` → `protocol_error` |
| BINARY | `Path: audio`, `Content-Type: audio/mpeg`, non-empty | `AudioChunk` |
| BINARY | `Path: audio`, no `Content-Type`, empty body | ignored |
| BINARY | all other conditions | `UnexpectedResponse` → `protocol_error` |
| ERROR | any | `WebSocketError` → `network_error` |

### Retry on HTTP 403 (DRM token rejection)

**Source:** `communicate.py Communicate.stream()` try/except block, `drm.py DRM.handle_client_response_error()`

```python
try:
    async for message in self.__stream():
        yield message
except aiohttp.ClientResponseError as e:
    if e.status != 403:
        raise
    DRM.handle_client_response_error(e)   # adjust clock skew from Date header
    async for message in self.__stream():  # one retry with corrected token
        yield message
```

**Rules:**
- **One retry** per chunk, only on HTTP 403 from the WebSocket upgrade (`ws_connect`).
- All other errors propagate immediately (no retry).
- The clock skew is adjusted from the server's `Date` response header, then the token is regenerated.

**C++ implementation:** `communication::RetryPolicy` + `SynthesisSession` retry path + `EdgeTokenProvider::adjust_clock_skew(double)`.

| Python | C++ |
|--------|-----|
| `e.status == 403` | `ErrorCode::drm_error` from `WebSocketClient::connect()` |
| `DRM.parse_rfc2616_date(date)` | `parse_http_date()` in `communication/HttpDate.hpp` |
| `DRM.adj_clock_skew_seconds(delta)` | `EdgeTokenProvider::adjust_clock_skew(seconds)` |
| `max_retries` | `RetryPolicy::max_retries` (default 1) |

**Date header extraction:** `ix::WebSocketInitResult::headers` is a `CaseInsensitiveLess` map that includes all HTTP response headers from the upgrade attempt. On HTTP 403, `WebSocketClient::connect()` reads `init.headers["Date"]` and stores it as the `context()` field of the `drm_error`. `SynthesisSession` then parses this with `parse_http_date()`.

**Clock skew formula** (matching Python `DRM.handle_client_response_error()`):
```
skew = server_time - (client_now + existing_skew)
EdgeTokenProvider::adjust_clock_skew(skew)
// After adjustment: total_skew = server_time - client_now
```

**Fallback behavior:** If the 403 response carries no Date header (context is empty) or the date string is malformed, the retry still proceeds — just without skew correction. This matches the Python reference where `parse_rfc2616_date` returns `None` and the adjustment is skipped.

**RFC 2616 date format:** `"Wkd, DD Mon YYYY HH:MM:SS GMT"` (e.g. `"Mon, 15 Jan 2024 08:31:15 GMT"`). Implemented in `communication::parse_http_date()` using Howard Hinnant's Gregorian day-count algorithm. Weekday and timezone fields are accepted but not validated.

### Close behavior

Python uses an `async with … as websocket:` context manager that closes on exit
(both success and error paths). C++ `SynthesisSession::synthesize()` calls
`websocket.close()` in a `finally`-style block: after the receive loop regardless
of success or error, and before returning any error.

### Proxy

`WebSocketClientOptions::proxy` stores an optional proxy URL, matching Python's
`proxy=self.proxy` passed to `ws_connect()`. The ixwebsocket synchronous API
has no CONNECT-tunnel proxy support, so if `proxy` is set, `connect()` returns
`ErrorCode::unsupported` before attempting the connection. Proxy is not
functional end-to-end; callers receive a clear error rather than a silent no-op.

---

## Connection and Request IDs

**Source:** `communicate.py connect_id()`, `__stream()`, `ssml_headers_plus_data()`

**C++ implementation:** `communication::ConnectionMetadata` struct +
`ConnectionMetadataFactory` class.

### Reference

```python
def connect_id() -> str:
    return uuid.uuid4().hex  # UUID v4 without hyphens, 32 lowercase hex chars
```

Usage in `__stream()`:
```python
async with session.ws_connect(
    f"{WSS_URL}&ConnectionId={connect_id()}"  # ← URL query param
    ...
) as websocket:
    # speech.config frame has NO request ID header
    await send_ssml_request()  # calls connect_id() again for X-RequestId
```

Usage in `ssml_headers_plus_data()`:
```python
f"X-RequestId:{request_id}\r\n"  # ← protocol frame header
```

### Format

| ID | Location | Format |
|----|----------|--------|
| `connection_id` | `&ConnectionId=` URL query param | UUID v4 without hyphens, 32 lowercase hex chars |
| `request_id` | `X-RequestId:` SSML frame header | UUID v4 without hyphens, 32 lowercase hex chars |

Both use `uuid.uuid4().hex` (Python), equivalent to C++ `IdGenerator::uuid_v4_without_hyphens()`.
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

**Reference files inspected:**
- `constants.py` — BASE_URL, TRUSTED_CLIENT_TOKEN, WSS_URL, VOICE_LIST,
  CHROMIUM_FULL_VERSION, SEC_MS_GEC_VERSION, BASE_HEADERS, WSS_HEADERS
- `communicate.py` — protocol frame Path values, content types
- `drm.py` — trusted client token usage in SHA-256 token generation
- `voices.py` — voice list endpoint usage

| Field | Value | Reference |
|-------|-------|-----------|
| `websocket_endpoint` | `wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4` | `constants.py WSS_URL` |
| `voices_endpoint` | `https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4` | `constants.py VOICE_LIST` |
| `trusted_client_token` | `6A5AA1D4EAFF4E9FB37E23D68491D6F4` | `constants.py TRUSTED_CLIENT_TOKEN` |
| `sec_ms_gec_version` | `1-143.0.3650.75` | `constants.py SEC_MS_GEC_VERSION` |
| `origin` | `chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold` | `constants.py WSS_HEADERS["Origin"]` |
| `user_agent` | `Mozilla/5.0 (Windows NT 10.0; Win64; x64) ... Chrome/143.0.0.0 ... Edg/143.0.0.0` | `constants.py BASE_HEADERS["User-Agent"]` |
| `speech_config_path` | `speech.config` | `communicate.py send_command_request()` |
| `ssml_path` | `ssml` | `communicate.py ssml_headers_plus_data()` |
| `audio_metadata_path` | `audio.metadata` | `communicate.py __stream()` |
| `turn_end_path` | `turn.end` | `communicate.py __stream()` |

**Case difference:** `WSS_URL` uses `TrustedClientToken` (mixed case) while
`VOICE_LIST` uses `trustedclienttoken` (lower case) — this matches the
reference exactly and must not be normalized.

**Dynamic params appended by callers** (not in config):
- WebSocket: `&ConnectionId=<uuid>&Sec-MS-GEC=<sha256>&Sec-MS-GEC-Version=<ver>`
- Voice list: `&Sec-MS-GEC=<sha256>&Sec-MS-GEC-Version=<ver>`

---

## Sec-MS-GEC Token Generation

**Source:** `drm.py DRM.generate_sec_ms_gec()`

**C++ implementation:** `communication::EdgeTokenProvider` (`EdgeTokenProvider.hpp` /
`EdgeTokenProvider.cpp`). SHA-256 helper: `common::sha256_hex_upper` (`Sha256.hpp` /
`Sha256.cpp`).

### Algorithm (exact Python reference)

```python
# drm.py
WIN_EPOCH = 11644473600  # seconds from 1601-01-01 to 1970-01-01 UTC
S_TO_NS = 1e9

ticks  = dt.now(tz.utc).timestamp() + DRM.clock_skew_seconds   # (1)
ticks += WIN_EPOCH                                               # (2)
ticks -= ticks % 300                                            # (3)
ticks *= S_TO_NS / 100                                          # (4) = 1e7
str_to_hash = f"{ticks:.0f}{TRUSTED_CLIENT_TOKEN}"              # (5)
return hashlib.sha256(str_to_hash.encode("ascii")).hexdigest().upper()  # (6)
```

C++ steps:

1. Get `double unix_seconds` from `IClock::now().time_since_epoch()`.
2. `double ticks = unix_seconds + 11644473600.0`
3. `ticks -= std::fmod(ticks, 300.0)` — rounds down to nearest 5-minute boundary.
4. `ticks *= 1e9 / 100.0` — same IEEE 754 double arithmetic as Python.
5. `snprintf(buf, "%.0f", ticks)` — same formatting as Python's `f"{ticks:.0f}"`.
6. Concatenate with `trusted_client_token` from `EdgeServiceConfig`.
7. `common::sha256_hex_upper(str_to_hash)` — SHA-256 (FIPS 180-4), uppercase hex.

### Float arithmetic compatibility

The `%.0f` format of both Python and C uses IEEE 754 round-to-nearest-even.
After step 3, `ticks` is an exact multiple of 300 (representable exactly in
double). After step 4, `ticks` may not be exactly representable, but both
runtimes round identically, producing the same string in step 5.

**Deterministic test proof:** three Python-generated fixed-clock vectors are
tested in `EdgeTokenProviderTests.cpp`:

| Unix timestamp | Expected token (uppercase hex SHA-256) |
|---|---|
| 0 | `7ECB79D14E3AA576D2D79E6D487A1388156D91E614B1BE11C64226A29BC8DD8C` |
| 1000000000 | `6594DCF2D741A251B0EDFB71C0034EBFEBF6D413CC1EA5D1B23E60B118A2F0E1` |
| 1700000000 | `42301B335578FEFDAE2637DED1ABD614505D432559EC08032B82048483726AFF` |

### Bucket boundary

Two timestamps in the same 300-second window produce the same token. The first
unix timestamp that starts a new window is the one where `(unix + WIN_EPOCH) % 300 == 0`.

### Clock skew

The Python reference accumulates `DRM.clock_skew_seconds` on 403 responses.
In C++, the `IClock` abstraction allows the communication layer to inject a
corrected clock without modifying `EdgeTokenProvider`.

### sec_ms_gec_version

`EdgeTokenProvider::sec_ms_gec_version()` returns
`config.sec_ms_gec_version` verbatim (= `"1-143.0.3650.75"`). No computation.

---

## UTC usage

Both modules use UTC exclusively:

- `drm.py`: `datetime.now(timezone.utc).timestamp()` — always UTC, never local time.
- `communicate.py`: `time.gmtime()` — always UTC.

`std::chrono::system_clock` is also UTC-based, so no conversion is needed at
the `IClock` level.

---

## Timestamp formats used

### DRM / `Sec-MS-GEC` token (`drm.py`)

1. Start with current UTC Unix time (float seconds since 1970-01-01 UTC).
2. Add clock skew correction (float seconds, initially 0.0).
3. Add Windows file time epoch offset: **`11644473600` seconds** — the number of
   seconds between 1601-01-01 00:00:00 UTC (Windows epoch) and 1970-01-01
   00:00:00 UTC (Unix epoch).
4. Round **down** to the nearest 5-minute boundary: `ticks -= ticks % 300`.
5. Convert to 100-nanosecond ticks: multiply by `1e9 / 100` using IEEE 754
   double arithmetic.
6. Concatenate the integer-formatted tick count with the trusted client token:
   `f"{ticks:.0f}6A5AA1D4EAFF4E9FB37E23D68491D6F4"`.
7. SHA-256 hash, return uppercase hex string.

This entire algorithm belongs in the DRM/token generation code
(**`serialization`** or **`communication`** layer), NOT in `IClock`.

### WebSocket `X-Timestamp` header (`communicate.py`)

JavaScript-style UTC date string, always in UTC regardless of locale:

```
Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)
```

Format string: `"%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)"`
via `time.strftime(..., time.gmtime())`.

A literal `Z` is then appended by the SSML envelope builder
(`ssml_headers_plus_data`).  This is documented as a known Microsoft Edge bug
in the source.

This formatting also belongs in the serialization/communication layer.

---

## Clock skew

The Python `DRM.clock_skew_seconds` class variable accumulates a correction
offset applied to every `get_unix_timestamp()` call.  On a 403 response from
the service, the `Date` response header (RFC 2616 format) is parsed, the
difference `server_date - client_date` is added to `clock_skew_seconds`.

**C++ design note:** `IClock` does not carry a skew field.  Instead, the token
generation code holds a mutable skew offset that is applied to `IClock::now()`
before the epoch conversion.  `FixedClock` makes this testable by setting the
exact `time_point` the token generator will observe.

---

## `IClock` contract

| Property | Value |
|----------|-------|
| Return type | `std::chrono::system_clock::time_point` |
| Epoch | 1970-01-01 00:00:00 UTC (same as Python `datetime.now(timezone.utc).timestamp()`) |
| Granularity | At least microsecond (system_clock resolution is platform-dependent, typically nanosecond) |
| Protocol constants | None — epoch offsets and tick conversions are done at the call site |

---

## SSML document format

**Source:** `communicate.py mkssml()`

The SSML document sent inside the WebSocket `ssml` path frame is a single
UTF-8 line with no inter-element whitespace:

```
<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'><voice name='{full_voice}'><prosody pitch='{pitch}' rate='{rate}' volume='{volume}'>{escaped_text}</prosody></voice></speak>
```

Key invariants:
- All attribute values use **single quotes** (Python f-string literals).
- `xml:lang` is **hardcoded `en-US`** regardless of voice locale.
- `<voice name>` always uses the **full form**: `Microsoft Server Speech Text to Speech Voice (locale, name)`.  Short-form voice names (`en-US-EmmaMultilingualNeural`) are normalized before embedding.
- Prosody attribute order: **pitch, rate, volume** (matches Python).
- Text content is **XML-escaped** (`&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`).
- The SSML body does **not** include WebSocket framing or `X-RequestId` / `X-Timestamp` headers — those are added by `ssml_headers_plus_data()` in the communication layer.

**C++ implementation:** `serialization::SsmlBuilder::build(config, raw_text)`:
1. Validates `TtsConfig` via `validate_tts_config()`.
2. Normalizes the voice name to the full form via `normalize_voice_name()`.
3. Normalizes `raw_text` via `TextNormalizer` (UTF-8 validation + control-char replacement).
4. XML-escapes the normalized text via `xml_escape()` exactly once.
5. Assembles the SSML string.

---

## Protocol Text Frame Format

**Source:** `communicate.py ssml_headers_plus_data()`, `send_command_request()`,
`get_headers_and_data()`

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

**SSML frame** (`ssml_headers_plus_data(request_id, timestamp, ssml)` in communicate.py):
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

**Speech config frame** (`send_command_request()` in communicate.py):
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
- `sentenceBoundaryEnabled` and `wordBoundaryEnabled` values are **JSON strings** (`"true"`/`"false"`), not JSON booleans — matches Python's f-string interpolation exactly.
- Boundary logic (from Python `send_command_request()`):
  - `WordBoundary` → `wordBoundaryEnabled:"true"`, `sentenceBoundaryEnabled:"false"`
  - `SentenceBoundary` (default) → `sentenceBoundaryEnabled:"true"`, `wordBoundaryEnabled:"false"`
- Output format is taken from `TtsConfig::output_format.value()` (not hardcoded).
- Body includes a trailing `\r\n` — matches the Python reference string exactly.
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

The Python reference parses via `get_headers_and_data(data, data.find(b"\r\n\r\n"))`:
- `data[:pos]` is the header section (split on `\r\n`, then each line on first `:`).
- The reference uses `data[pos + 2:]` for the body — this is a known off-by-two:
  it leaves a leading `\r\n` prefix that Python's `json.loads` silently discards.
- The C++ `ProtocolParser` correctly uses `pos + 4` (full `\r\n\r\n` skip) to
  produce a clean body with no prefix.
- Headers are returned as `vector<pair<string,string>>` (preserves duplicates and
  ordering), unlike the Python dict (which overwrites earlier duplicate values).

### Incoming binary frame format

```
[HL_MSB, HL_LSB, header_content (HL-2 bytes), \r\n, body_bytes...]
```

- `HL` = big-endian uint16 from bytes 0–1 = total size of prefix + header content
  (i.e., HL = 2 + len(header_content_without_trailing_CRLF))
- Header content at bytes `[2 .. HL)` — parsed without the 2-byte length prefix
- `\r\n` separator at bytes `[HL .. HL+2)`
- Body at bytes `[HL+2 ..)`

**Python quirk:** `get_headers_and_data(data, HL)` slices `data[:HL]` which includes
the 2-byte length prefix. Splitting on `\r\n` makes the first header line include that
prefix (e.g., `b"\x00\x24X-RequestId:abc"`). The code relies on the service always
placing a "don't care" header (like `X-RequestId`) FIRST so that `Path` and
`Content-Type` are on subsequent (correctly-parsed) lines. The C++ implementation
parses headers from byte 2 onwards, so all headers parse correctly regardless of order.

**Binary frame validation:**

Rows marked **(stricter)** are checks the Python reference does not perform.
The Python `get_headers_and_data()` would instead crash with `ValueError`, silently
yield an empty body, or produce garbled headers; the C++ parser rejects these cases
deterministically with `protocol_error`.

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

**Source:** `communicate.py Communicate.__stream()` and `stream()`

**C++ implementation:** `communication::SynthesisSession::synthesize(tts_config, text_chunks)`

### Per-chunk sequence (matches `__stream()` exactly)

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

`stream()` iterates over all text chunks and calls `__stream()` per chunk.
C++ `SynthesisSession::synthesize` replicates this: for each chunk, the full
lifecycle in step 1–9 is repeated with a **new** WebSocket connection.

### Error propagation

| Step that fails | Python exception | C++ ErrorCode |
|-----------------|-----------------|---------------|
| `sec_ms_gec()` | — | `protocol_error` |
| `connect()` | transport error | `network_error` |
| `send_text()` | transport error | `network_error` |
| `receive()` | transport error | `network_error` |
| `parse_incoming()` | `UnknownResponse` / `UnexpectedResponse` | `protocol_error` |
| no audio received | `NoAudioReceived` | `service_error` |

On any error after `connect()`, `close()` is always called before the error is
returned (matching Python's context manager `__aexit__`). The close result is
silently discarded.

---

### Metadata all-SessionEnd deviation

Python's `__parse_metadata()` raises `UnexpectedResponse("No WordBoundary metadata found")`
when the Metadata array is empty or all-`SessionEnd`. C++ `MetadataJsonParser` returns
an empty vector in that case; `parse_incoming` treats an empty result as `protocol_error`
to match reference behavior.

---

## Metadata Frame JSON Format

**Source:** `communicate.py Communicate.__parse_metadata()`, submaker.py

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

Note: the Python reference raises `UnknownResponse` for unknown types; the C++
implementation deviates intentionally by skipping them at the `MetadataJsonParser`
level.  `EdgeProtocol` applies a safety net: if the resulting boundary list is
empty it returns `protocol_error("No WordBoundary metadata found")`, which
matches the reference error for the all-unknown and all-`SessionEnd` cases.
This deviation enables forward compatibility when the service introduces new event
types (e.g. `VisemeBoundary`) without breaking synthesis.

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

**Deviation from Python reference:** The Python `__compensate_offset()` used
`cumulative_audio_bytes * 8 * 10_000_000 / 48_000` (fixed 48 kbps bitrate).
The C++ implementation replaces this with the metadata-derived formula above,
which is format-independent and produces identical results when boundary
metadata is present.

### XML unescape

The `"Text"` field value is XML-unescaped using `xml.sax.saxutils.unescape()`
in Python. The C++ implementation uses `serialization::xml_unescape()`:
- `&amp;` → `&`
- `&lt;` → `<`
- `&gt;` → `>`
- `&quot;` → `"`
- `&apos;` → `'`

---

## Timing constants (belong in serialization/communication layer)

| Constant | Value | Source |
|----------|-------|--------|
| Windows file time epoch offset | `11644473600` seconds | `drm.py: WIN_EPOCH` |
| Nanoseconds per second | `1e9` | `drm.py: S_TO_NS` |
| Tick duration | 100 nanoseconds | `drm.py`, Edge TTS wire format |
| Ticks per second | `10,000,000` | `constants.py: TICKS_PER_SECOND` |
| Token round-down boundary | 300 seconds (5 minutes) | `drm.py: generate_sec_ms_gec()` |
| Audio bitrate (Python reference, not used in C++) | 48,000 bps | `constants.py: MP3_BITRATE_BPS` |
