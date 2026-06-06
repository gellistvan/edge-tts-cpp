# Third-Party Dependencies

This document lists all third-party libraries used by `edge-tts-cpp`, their
source, how they are integrated, and which modules consume them.

---

## Dependency resolution

All third-party dependencies follow a strict lookup order to support both
submodule-based development checkouts and source archives / CI environments
that cannot rely on submodules being populated.

### Lookup order (per dependency)

1. **Submodule present** — `submodules/<name>/CMakeLists.txt` exists →
   `add_subdirectory` (preferred, deterministic, offline-capable).
2. **System/package-manager install** — `find_package(... CONFIG QUIET)` →
   use the installed package (vcpkg, conan, apt, Homebrew, etc.).
3. **FetchContent auto-download** — `EDGE_TTS_FETCH_DEPS=ON` →
   `FetchContent_Declare` + `FetchContent_MakeAvailable` pulls the pinned
   tag from GitHub at configure time.
4. **Not found** → `message(FATAL_ERROR ...)` with an actionable message.

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_TTS_FETCH_DEPS` | `ON` | Allow FetchContent to download missing dependencies automatically |
| `EDGE_TTS_REQUIRE_NETWORKING` | `ON` when `EDGE_TTS_BUILD_APPS=ON`, else `OFF` | Treat missing ixwebsocket as a fatal configure error |

### Release / source archives

Source archives (e.g. GitHub tarballs) do not include submodule contents.
Use one of:
- `EDGE_TTS_FETCH_DEPS=ON` (default) — CMake downloads pinned dependency versions at configure time.
- Vendor the dependency sources manually into `submodules/`.
- Pre-install via a package manager and let `find_package` locate them.

---

## minigtest

| Property | Value |
|----------|-------|
| Source | Vendored single-header at `tests/vendor/minigtest/minigtest.hpp` |
| Purpose | Minimal GTest-compatible test runner for the project's unit tests |
| Integration | Header-only; `edge_tts_add_module_test()` adds `tests/` to the include path |
| Consumers | All test executables |
| License | See file header |

Supported macros: `TEST`, `EXPECT_EQ/NE/TRUE/FALSE`, `ASSERT_EQ/NE/TRUE/FALSE`,
`EXPECT_THROW`, `EXPECT_NO_THROW`.

---

## nlohmann/json

| Property | Value |
|----------|-------|
| Source | Git submodule at `submodules/json` (https://github.com/nlohmann/json) |
| Purpose | JSON parsing for the voice-list API response |
| Integration | `cmake/Dependencies.cmake` adds `submodules/json` via `add_subdirectory` when the submodule is present; the top-level `CMakeLists.txt` links `nlohmann_json::nlohmann_json` to `edge_tts_serialization` and `edge_tts_communication` when the target exists |
| Consumers | `edge_tts::serialization` (`VoiceJsonParser`), `edge_tts::communication` (future voice-list HTTP layer) |
| License | MIT (`submodules/json/LICENSE.MIT`) |

To initialise the submodule after a fresh clone:

```sh
git submodule update --init submodules/json
```

The `JSON_BuildTests` CMake option is forced `OFF` so nlohmann's own test suite
is not built.

---

## POSIX process API (`fork` / `execvp` / `pipe` / `waitpid`)

| Property | Value |
|----------|-------|
| Source | POSIX standard library — always present on Linux |
| Purpose | `media::ProcessRunner` — runs external commands (mpv, ffmpeg, edge-tts) as child processes with no shell involvement |
| Integration | Included via `<unistd.h>`, `<sys/wait.h>` directly in `src/media/ProcessRunner.cpp`; `std::thread` drains stderr concurrently |
| Consumers | `edge_tts::media` (`ProcessRunner`) |
| License | System library; no additional license obligation |

Reference behavior: Python's `subprocess.Popen(list_of_args)` — list-form prevents shell injection and word-splitting of arguments containing spaces.  `ProcessRunner::run()` uses the same safe pattern via `execvp()`.

---

## std::filesystem (C++17/20 standard library)

| Property | Value |
|----------|-------|
| Source | C++20 standard library — always present on all supported platforms |
| Purpose | `media::ExecutableDiscovery` — PATH scanning to locate `mpv`, `ffmpeg`, `edge-tts` and other external tools without spawning any child process |
| Integration | Included via `<filesystem>` in `include/edge_tts/media/ExecutableDiscovery.hpp` and `src/media/ExecutableDiscovery.cpp`; available through the module's existing `cxx_std_20` compile feature |
| Consumers | `edge_tts::media` (`ExecutableDiscovery`) |
| License | System library; no additional license obligation |

Reference behavior: Python's `shutil.which()` used in `edge_playback/__main__.py _check_deps()` to verify `mpv` and `edge-tts` are installed.  `ExecutableDiscovery::find_on_path()` replicates that PATH scan deterministically without process execution.

`FfmpegAudioConverter` uses this to locate `ffmpeg` (format conversion) and `ffplay` (audio playback) at runtime.  No `libavcodec`, `libavformat`, or any other FFmpeg library is ever linked — all interaction goes through `IProcessRunner`.

---

## ixwebsocket (IXWebSocket)

| Property | Value |
|----------|-------|
| Source | Git submodule at `submodules/ixwebsocket` (https://github.com/machinezone/IXWebSocket) |
| Purpose | WebSocket client (`communication::WebSocketTransport`) and HTTP client (`communication::IHttpClient` implementation) for the voice-list and synthesis endpoints |
| Integration | `cmake/EdgeTtsDependencies.cmake` adds `submodules/ixwebsocket` via `add_subdirectory(… EXCLUDE_FROM_ALL)` when the submodule is present; the top-level `CMakeLists.txt` links `ixwebsocket` to `edge_tts_communication` when the target exists |
| Consumers | `edge_tts::communication` (WebSocketTransport, HTTP voice-list client) |
| License | BSD 3-Clause (`submodules/ixwebsocket/LICENSE`) |
| CMake target | `ixwebsocket` |
| Required when | `EDGE_TTS_REQUIRE_NETWORKING=ON` (default when `EDGE_TTS_BUILD_APPS=ON`) |
| Optional when | `EDGE_TTS_REQUIRE_NETWORKING=OFF` — stub `FakeHttpClient`/`FakeWebSocketClient` compile without ixwebsocket |

**Why ixwebsocket over alternatives:**

| Criterion | ixwebsocket | cpr | libcurl |
|-----------|-------------|-----|---------|
| Single submodule for both HTTP and WebSocket | ✓ | ✗ (HTTP only) | ✗ (HTTP only) |
| Already planned in this project | ✓ | ✗ | ✗ |
| Header/source library; no OS install required | ✓ | ✗ (needs curl) | ✗ (system install) |
| Aligns with Python reference (aiohttp = HTTP+WS) | ✓ | partial | partial |

Choosing ixwebsocket avoids a second WebSocket submodule later and matches the
project's existing plan documented in `submodules/README.md`.

**Warning suppression:** `cmake/EdgeTtsDependencies.cmake` sets
`INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` on the `ixwebsocket` target so that our
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` flags are silenced on
ixwebsocket headers.  Downstream targets inherit this automatically.

**Public header isolation:** ixwebsocket types are only used in `src/communication/`
implementation files. No ixwebsocket header appears in `include/edge_tts/`; the
`IHttpClient` and `IWebSocketClient` interfaces use only standard library types.

### Initializing the submodule

After a fresh clone or when `submodules/ixwebsocket/` is empty:

```sh
git submodule update --init submodules/ixwebsocket
```

To initialize all submodules at once:

```sh
git submodule update --init --recursive
```

### Updating ixwebsocket

```sh
cd submodules/ixwebsocket
git fetch origin
git checkout <desired-tag-or-commit>
cd ../..
git add submodules/ixwebsocket
git commit -m "chore: bump ixwebsocket to <version>"
```

### WebSocketClient integration

`communication::WebSocketClient` (in `include/edge_tts/communication/WebSocketClient.hpp` /
`src/communication/WebSocketClient.cpp`) implements `IWebSocketClient` using
`ix::WebSocket` from ixwebsocket.  The ixwebsocket types are confined to
`WebSocketClient.cpp` behind a Pimpl idiom — no ixwebsocket header appears in
the public interface.

`WebSocketClientOptions` controls:
- `connect_timeout` — maps to `ix::WebSocket::setHandshakeTimeout()` (default 10 s, reference `sock_connect=10`)
- `read_timeout` — used as the `wait_for` deadline in the blocking `receive()` call (default 60 s, reference `sock_read=60`)
- `proxy` — stored for callers; ixwebsocket's synchronous API does not expose WebSocket CONNECT-tunnel proxy, so this is not yet forwarded
- `extra_headers` — forwarded to `ix::WebSocket::setExtraHeaders()` for the upgrade request (reference: `WSS_HEADERS` in `constants.py`)

After a successful `connect()`, an internal thread runs `ws.run()` and pumps
incoming frames into a thread-safe queue.  `receive()` blocks on that queue until
a frame arrives or `read_timeout` elapses.  `close()` calls `ws.stop()` and
joins the receive thread.

TLS: `tls_opts.caFile = "SYSTEM"` tells ixwebsocket to use the platform CA
bundle (matching Python's `ssl.create_default_context()` in `communicate.py`).

### HttpClient integration

`communication::HttpClient` (in `include/edge_tts/communication/HttpClient.hpp` /
`src/communication/HttpClient.cpp`) implements `IHttpClient` using
`ix::HttpClient` from ixwebsocket.  The ixwebsocket types are confined to
`HttpClient.cpp` and never appear in the public header.

Error mapping from `ix::HttpErrorCode`:

| ixwebsocket error | `ErrorCode` | Notes |
|-------------------|-------------|-------|
| `UrlMalformed`, `Invalid` | `invalid_argument` | Detected without touching the network |
| `Timeout` | `timeout` | connect or transfer deadline exceeded |
| all other non-OK codes | `network_error` | TCP/TLS transport failure |
| `Ok` | (success) | HTTP status code in `HttpResponse.status_code` |

Non-2xx HTTP status codes are returned as successful `Result<HttpResponse>` values
so that callers (e.g. `VoiceService`) can inspect and act on them.

`HttpClientOptions.proxy` is stored but currently not forwarded to
`ix::HttpClient` because ixwebsocket's synchronous HTTP client does not expose
per-request proxy configuration.  This will be addressed when WebSocket transport
is wired in (ixwebsocket WebSocket supports `CONNECT`-tunnel proxy).

### TLS

`EdgeTtsDependencies.cmake` sets `USE_TLS=ON` by default so the library builds
with TLS support (required for HTTPS/WSS connections to the Edge TTS service).
On Linux and macOS it picks up the system OpenSSL via `find_package(OpenSSL)`.

To disable TLS (e.g. for isolated LAN testing):

```sh
cmake -S . -B build -DUSE_TLS=OFF
```

To select mbedTLS instead of OpenSSL:

```sh
cmake -S . -B build -DUSE_TLS=ON -DUSE_MBED_TLS=ON
```

---

## Planned / Not Yet Integrated

| Library | Submodule path | Purpose | Status |
|---------|----------------|---------|--------|
| CLI11 | `submodules/CLI11` | Command-line argument parsing for `edge-tts` / `edge-playback` | Not yet added |
| googletest | `submodules/googletest` | Alternative test runner (optional, currently unused) | Conditional in `Dependencies.cmake` |
