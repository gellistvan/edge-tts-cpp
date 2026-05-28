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

## Timing constants (belong in serialization/communication layer)

| Constant | Value | Source |
|----------|-------|--------|
| Windows file time epoch offset | `11644473600` seconds | `drm.py: WIN_EPOCH` |
| Nanoseconds per second | `1e9` | `drm.py: S_TO_NS` |
| Tick duration | 100 nanoseconds | `drm.py`, Edge TTS wire format |
| Ticks per second | `10,000,000` | `constants.py: TICKS_PER_SECOND` |
| Token round-down boundary | 300 seconds (5 minutes) | `drm.py: generate_sec_ms_gec()` |
| Audio bitrate (for offset compensation) | 48,000 bps | `constants.py: MP3_BITRATE_BPS` |
