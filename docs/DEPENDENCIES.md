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
| `EDGE_TTS_FETCH_DEPS` | `OFF` | Allow FetchContent to download missing dependencies automatically. Default `OFF` so a missing dependency fails at configure time with a clear, actionable message rather than a confusing git/network error. Set `ON` for online developer builds or CI. |
| `EDGE_TTS_REQUIRE_NETWORKING` | `ON` when `EDGE_TTS_BUILD_APPS=ON`, else `OFF` | Treat missing ixwebsocket as a fatal configure error |

### Failure behavior

When a dependency cannot be found via any of the three sources above, CMake
aborts at **configure time** with a `FATAL_ERROR` message that names the missing
package and lists concrete remediation steps.  No ambiguous compile-time error
is ever produced for a missing dependency.

### Release / source archives

GitHub automatic "Source code" archives do not include submodule contents —
`submodules/json/` and `submodules/ixwebsocket/` are empty directories.

Options for building from such an archive:

1. **Official release tarball** (`edge-tts-cpp-<VERSION>.tar.gz`): created with
   `tools/make_release_archive.sh`, which populates submodule directories before
   packing.  Configure succeeds offline without any additional steps.
2. **System package manager**: `sudo apt install nlohmann-json3-dev` (and install
   ixwebsocket if CLI apps are needed).  Use `EDGE_TTS_FETCH_DEPS=OFF`.
3. **FetchContent**: `cmake -DEDGE_TTS_FETCH_DEPS=ON ...`.  Requires internet
   access at configure time; downloads the pinned tag for each missing dep.

See [`docs/RELEASE.md`](RELEASE.md) for the complete source archive policy and
the release checklist.

---

## add_subdirectory safety

edge-tts-cpp is safe to consume via `add_subdirectory` from a parent CMake project.

### Canonical directory variables

All project-local file paths use `EDGE_TTS_SOURCE_DIR` and `EDGE_TTS_BINARY_DIR`
instead of `CMAKE_SOURCE_DIR` and `CMAKE_BINARY_DIR`.

These variables are defined at the top of `CMakeLists.txt`:

```cmake
set(EDGE_TTS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")
set(EDGE_TTS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")
```

`CMAKE_CURRENT_SOURCE_DIR` and `CMAKE_CURRENT_BINARY_DIR` refer to
edge-tts-cpp's own directory regardless of nesting depth, so the parent
project's `CMAKE_SOURCE_DIR` is never touched.

A regression check (`tests/cmake/test_cmake_source_dir_regression.py`, CTest
name `edge_tts_cmake_source_dir_regression`) scans all of edge-tts-cpp's own
cmake files and fails the build if any bare `${CMAKE_SOURCE_DIR}` or
`${CMAKE_BINARY_DIR}` reference is introduced.  The check excludes the
`submodules/` directory (third-party projects have their own conventions) and
the consumer fixture under `tests/cmake/consumer_add_subdirectory_basic/`.

### Consumer fixture

A minimal consumer project lives in `tests/cmake/consumer_add_subdirectory_basic/`.
It is a standalone CMake project that:

1. Calls `add_subdirectory(edge-tts-cpp …)` with `EDGE_TTS_CPP_SOURCE_DIR` pointing
   to the repo root.
2. Links against `edge_tts::common` and `edge_tts::core`.
3. Asserts at configure time that `CMAKE_SOURCE_DIR` still points to the consumer's
   own root (not inside edge-tts-cpp).
4. Compiles a small C++ binary that uses `edge_tts/common/Error.hpp` and
   `edge_tts/core/Voice.hpp` to confirm that the public include path is correct.

The Python test `tests/cmake/test_consumer_add_subdirectory.py` (CTest name
`edge_tts_consumer_add_subdirectory_tests`) configures and builds this fixture in
a temporary directory.

---

## cmake --install support

edge-tts-cpp ships CMake install rules (enabled via `EDGE_TTS_INSTALL=ON`, the
default when this is the top-level project).

### Installed targets

| Target (installed name) | Alias created by config | Type |
|-------------------------|------------------------|------|
| `edge_tts_tts` | `edge_tts::tts` | INTERFACE (entry point) |
| `edge_tts_api` | `edge_tts::api` | STATIC |
| `edge_tts_communication` | `edge_tts::communication` | STATIC |
| `edge_tts_serialization` | `edge_tts::serialization` | STATIC |
| `edge_tts_subtitle` | `edge_tts::subtitle` | STATIC |
| `edge_tts_core` | `edge_tts::core` | STATIC |
| `edge_tts_common` | `edge_tts::common` | STATIC |
| `ixwebsocket` | — | STATIC (when compiled with ixwebsocket) |

The export set is named `edge_tts_cpp_targets`; the CMake package name is
`edge_tts_cpp`.  Targets are exported **without** the `edge_tts::` namespace
prefix to avoid double-prefixing; the generated `edge_tts_cpp-config.cmake`
creates `edge_tts::<name>` ALIAS targets from the imported `edge_tts_<name>`
names via `set_target_properties(IMPORTED_GLOBAL TRUE)` + `add_library(ALIAS)`.

### Transitive dependencies of installed targets

| Dependency | Why needed at install |
|------------|----------------------|
| `ixwebsocket` | `edge_tts_communication.a` references ixwebsocket symbols; included in the export set when compiled |
| `ZLIB::ZLIB` | ixwebsocket (TLS/gzip) references `ZLIB::ZLIB`; `edge_tts_cpp-config.cmake` calls `find_dependency(ZLIB QUIET)` |
| `Threads::Threads` | ixwebsocket references `Threads::Threads`; `edge_tts_cpp-config.cmake` calls `find_dependency(Threads QUIET)` |
| `nlohmann/json` | header-only; injected via `target_include_directories PRIVATE` so it does **not** appear in the INTERFACE_LINK_LIBRARIES of installed targets — consumers do not need it unless they parse Edge protocol JSON themselves |

### ixwebsocket headers

ixwebsocket's CMakeLists.txt sets `PUBLIC_HEADER` to relative paths
(`ixwebsocket/IXBase64.h`, …) that are interpreted relative to the **parent**
project root during `cmake_install.cmake` execution.  To avoid this,
`EdgeTtsInstall.cmake` clears the `PUBLIC_HEADER` property before the
`install(TARGETS ixwebsocket …)` call and uses a direct
`install(DIRECTORY submodules/ixwebsocket/ixwebsocket DESTINATION include)`
instead, producing `<prefix>/include/ixwebsocket/*.h`.

### Install test

`tests/cmake/test_install_tree.py` (CTest name `edge_tts_install_tree_tests`):
1. Configures edge-tts-cpp with `EDGE_TTS_BUILD_APPS=OFF -DEDGE_TTS_INSTALL=ON`.
2. Builds all production library targets.
3. Runs `cmake --install`.
4. Verifies required headers and CMake package files are present.
5. Checks no fake/test-support headers were installed.
6. Configures `tests/cmake/consumer_install_basic/` against the install prefix
   to verify `find_package(edge_tts_cpp)` and all `edge_tts::` aliases work.

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
| Source | POSIX standard library — always present on Linux and macOS |
| Purpose | `media::ProcessRunner` — runs external commands (ffplay, ffmpeg) as child processes with no shell involvement |
| Integration | Included via `<unistd.h>`, `<sys/wait.h>` in `src/media/ProcessRunner.cpp`; only compiled on non-Windows (CMake `if(NOT WIN32)`); `std::thread` drains stderr concurrently |
| Consumers | `edge_tts::media` (`ProcessRunner`), `edge-playback` app |
| License | System library; no additional license obligation |
| Platform | **POSIX only.** `ProcessRunner.cpp` is excluded from the Windows build by CMake and emits `#error` as a compile-time safety net. `FakeProcessRunner.cpp` (no POSIX deps) is always compiled and available on all platforms for testing. |

**CMake options for platform support:**

| Scenario | CMake flags |
|----------|------------|
| Linux/macOS — full build | defaults (both apps enabled) |
| Windows — library + edge-tts only | `-DEDGE_TTS_BUILD_PLAYBACK_APP=OFF` (default on Windows) |
| Windows — explicitly disable playback | `-DEDGE_TTS_BUILD_PLAYBACK_APP=OFF` |
| Windows — request playback (unsupported) | `EDGE_TTS_BUILD_PLAYBACK_APP=ON` → **configure FATAL_ERROR** naming the platform |

Setting `EDGE_TTS_BUILD_PLAYBACK_APP=ON` on Windows triggers a fatal CMake error that names the platform (`${CMAKE_SYSTEM_NAME}`) and explains the POSIX requirement. The core library (`common`, `core`, `serialization`, `communication`, `api`, `cli`) and the `edge-tts` CLI build cleanly on Windows without any POSIX dependency.

Reference behavior: Python's `subprocess.Popen(list_of_args)` — list-form prevents shell injection and word-splitting of arguments containing spaces.  `ProcessRunner::run()` uses the same safe pattern via `execvp()`.

`ProcessRunner` is not marked `final` so tests can subclass it and override `make_pipe(int fds[2])` to inject pipe-creation failures without spawning real child processes.

**Descriptor cleanup on error paths:**

If the stdout pipe succeeds but the stderr pipe fails, the stdout write-end is closed manually before returning and the read-end is closed by its RAII `FdCloser`. This prevents file-descriptor leaks on the pipe-failure path. The `FailSecondPipeRunner` test fixture in `tests/media/ProcessRunnerTests.cpp` verifies this behavior by overriding `make_pipe()` to fail on the second call.

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
| Purpose | WebSocket client (`communication::WebSocketClient`) and HTTP client (`communication::HttpClient` via `communication::IHttpClient`) for the voice-list and synthesis endpoints |
| Integration | `cmake/EdgeTtsDependencies.cmake` adds `submodules/ixwebsocket` via `add_subdirectory(… EXCLUDE_FROM_ALL)` when the submodule is present; the top-level `CMakeLists.txt` links `ixwebsocket` to `edge_tts_communication` when the target exists |
| Consumers | `edge_tts::communication` (`WebSocketClient`, `HttpClient` for voice-list and synthesis) |
| License | BSD 3-Clause (`submodules/ixwebsocket/LICENSE`) |
| CMake target | `ixwebsocket` |
| Required when | `EDGE_TTS_REQUIRE_NETWORKING=ON` (default when `EDGE_TTS_BUILD_APPS=ON`) |
| Optional when | `EDGE_TTS_REQUIRE_NETWORKING=OFF` — `FakeHttpClient`/`FakeWebSocketClient` (test doubles) compile without ixwebsocket |

**Why ixwebsocket over alternatives:**

| Criterion | ixwebsocket | cpr | libcurl |
|-----------|-------------|-----|---------|
| Single submodule for both HTTP and WebSocket | ✓ | ✗ (HTTP only) | ✗ (HTTP only) |
| Already integrated in this project | ✓ | ✗ | ✗ |
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
- `proxy` — if set, `connect()` returns `ErrorCode::unsupported` before touching the network (ixwebsocket has no CONNECT-tunnel proxy API)
- `extra_headers` — forwarded to `ix::WebSocket::setExtraHeaders()` for the upgrade request (reference: `WSS_HEADERS` in `constants.py`)

After a successful `connect()`, an internal thread runs `ws.run()` and pumps
incoming frames into a thread-safe queue.  `receive()` blocks on that queue until
a frame arrives or `read_timeout` elapses.  `close()` calls `ws.stop()` and
joins the receive thread.

TLS: `tls_opts.caFile = "SYSTEM"` tells ixwebsocket to use the platform CA
bundle (matching Python's `ssl.create_default_context()` in `communicate.py`).

### Production app usage

`communication::HttpClient` is the HTTP backend used in production by
`apps/edge-tts/main.cpp` for `--list-voices`.  It is constructed with
`HttpClientOptions` carrying the proxy and timeout from `CommunicateOptions`
defaults, and is backed by `ix::HttpClient` from ixwebsocket.
`FakeHttpClient` is only used in tests and is never compiled into the app.

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

**Proxy**: ixwebsocket's synchronous HTTP client (`ix::HttpClient`) has no
per-request proxy API.  If `HttpClientOptions::proxy` is set, `send()` returns
`ErrorCode::unsupported` before making any network call.  This is an explicit
rejection, not a silent no-op — callers receive a clear error and can surface it
to the user (CLI exit 1).

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

## Not Integrated / Not Planned

The following libraries were considered during the initial design and are **not** used:

| Library | Reason not used |
|---------|----------------|
| CLI11 | Hand-rolled argument parser (`EdgeTtsArgumentParser`, `PlaybackArgumentParser`) matches the Python `argparse` behavior exactly; a third-party CLI framework is not needed. |
| googletest | Replaced by `minigtest` — a self-contained GTest-compatible single-header at `tests/vendor/minigtest/minigtest.hpp`. No external test framework installation is required. |
