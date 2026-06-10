# Consuming edge-tts-cpp as a Library

This document describes the supported ways to integrate edge-tts-cpp into a
CMake project.

## Platform prerequisites

| Platform | Library | `edge-tts` CLI | `edge-playback` CLI | Notes |
|----------|---------|----------------|---------------------|-------|
| Linux / macOS | ✓ | ✓ | ✓ | Full support. Requires system OpenSSL for TLS. |
| Windows | ✓ | ✓ | ✗ | Core library and `edge-tts` CLI build cleanly. `edge-playback` is POSIX-only (`ProcessRunner` uses `fork`/`execvp`). Set `-DEDGE_TTS_BUILD_PLAYBACK_APP=OFF` (default on Windows). TLS uses ixwebsocket's bundled mbedtls — no extra install. |

The entire synthesis pipeline (`SpeechSynthesizer`, `list_voices()`, all of `api`, `communication`, `core`, `serialization`, `subtitles`) is cross-platform. Only `media::ProcessRunner` (which drives `ffplay` for audio playback) is POSIX-only. To add Windows playback support, implement `edge_tts::media::IProcessRunner` using `CreateProcess` and link it to `edge_tts::media`.

Ready-to-copy examples:
- [`examples/consumer_add_subdirectory/`](../examples/consumer_add_subdirectory/) — full add_subdirectory example
- [`examples/consumer_find_package/`](../examples/consumer_find_package/) — full find_package example

---

## Quick start

```cmake
# After installation (Option A below):
find_package(edge_tts_cpp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

```cpp
#include <edge_tts/edge_tts.hpp>   // recommended umbrella header

edge_tts::core::TtsConfig cfg;
cfg.voice = "en-US-EmmaMultilingualNeural";

edge_tts::api::SpeechSynthesizer tts("Hello, world!", std::move(cfg), {});
auto result = tts.save("hello.mp3", "hello.srt");
if (!result) {
    std::cerr << result.error().what() << '\n';
}
```

That's it.  `edge_tts::tts` carries all required link dependencies and include
paths automatically — no need to list ixwebsocket, nlohmann_json, or internal
sub-targets.

---

## Recommended header

Always include the umbrella header:

```cpp
#include <edge_tts/edge_tts.hpp>
```

It exposes the complete stable public API:

| Symbol | Purpose |
|--------|---------|
| `edge_tts::api::SpeechSynthesizer` | Synthesize text to audio |
| `edge_tts::api::SynthesisOptions` | Transport options (timeouts) |
| `edge_tts::api::FileWriter` | Write audio/subtitle files |
| `edge_tts::api::list_voices(opts)` | Fetch available voices from the service |
| `edge_tts::core::TtsConfig` | Voice, rate, volume, pitch |
| `edge_tts::core::Voice` | Voice listing type |
| `edge_tts::core::AudioChunk` | Raw MP3 bytes from `synthesize()` |
| `edge_tts::core::BoundaryChunk` | Word/sentence boundary event |
| `edge_tts::core::is_audio(chunk)` | `true` for `AudioChunk` variants |
| `edge_tts::common::Result<T>` | Error propagation (no exceptions for runtime errors) |
| `edge_tts::common::ErrorCode` | Error category enum |

Individual headers remain available for finer-grained includes:
`edge_tts/api/SpeechSynthesizer.hpp`, `edge_tts/core/TtsConfig.hpp`, etc.

### Stable public headers

The following headers are part of the stable consumer-facing API, are
self-contained, and are safe to include individually.  Each is guaranteed to
compile as the first (and only) project include in a translation unit.

#### `edge_tts/api/` — synthesis facade

| Header | Key types |
|--------|-----------|
| `edge_tts/api/SpeechSynthesizer.hpp` | `SpeechSynthesizer` — single-use synthesis object |
| `edge_tts/api/SynthesisOptions.hpp` | `SynthesisOptions` — transport options |
| `edge_tts/api/FileWriter.hpp` | `FileWriter` — write MP3 + SRT |
| `edge_tts/api/VoiceList.hpp` | `list_voices()` — fetch available voices |

#### `edge_tts/core/` — data types

| Header | Key types |
|--------|-----------|
| `edge_tts/core/TtsConfig.hpp` | `TtsConfig` — voice, rate, volume, pitch |
| `edge_tts/core/Voice.hpp` | `Voice` — voice listing entry |
| `edge_tts/core/Chunk.hpp` | `AudioChunk`, `BoundaryChunk`, `TtsChunk` variant, `is_audio()` |
| `edge_tts/core/TtsChunk.hpp` | `TtsChunk` alias |
| `edge_tts/core/OutputFormat.hpp` | `OutputFormat` — audio format selection |

#### `edge_tts/common/` — error handling and utilities

| Header | Key types |
|--------|-----------|
| `edge_tts/common/Result.hpp` | `Result<T>` — error propagation without exceptions |
| `edge_tts/common/Error.hpp` | `ErrorCode` — error category enum |
| `edge_tts/common/Errors.hpp` | `make_error()`, `error_message()` helpers |
| `edge_tts/common/Expected.hpp` | `Expected<T,E>` — internal result type |
| `edge_tts/common/Clock.hpp` | `IClock`, `SystemClock` |
| `edge_tts/common/IdGenerator.hpp` | `IIdGenerator`, UUID generation |
| `edge_tts/common/Hex.hpp` | `hex_encode()` |
| `edge_tts/common/Sha256.hpp` | `sha256_hex()` |
| `edge_tts/common/Utf8.hpp` | UTF-8 validation helpers |

#### `edge_tts/subtitles/` — subtitle generation

| Header | Key types |
|--------|-----------|
| `edge_tts/subtitles/SubMaker.hpp` | `SubMaker` — collect boundaries, write SRT |
| `edge_tts/subtitles/SubtitleCue.hpp` | `SubtitleCue` |
| `edge_tts/subtitles/Subtitle.hpp` | `Subtitle` |
| `edge_tts/subtitles/SubtitleTime.hpp` | `SubtitleTime` |
| `edge_tts/subtitles/SrtComposer.hpp` | `SrtComposer` — format SRT text |

### Installed but not stable-API headers

These headers are installed under `include/edge_tts/` and are self-contained,
but they are **not part of the stable public API** and may change between minor
versions.  Use them only when building a custom transport layer or extending
the library:

| Module | Purpose |
|--------|---------|
| `edge_tts/communication/` | Internal WebSocket transport, HTTP client, retry policy, synthesis session |
| `edge_tts/serialization/` | Internal protocol framing, SSML builder, text chunker |
| `edge_tts/media/` | ffmpeg/ffplay audio conversion (app-layer) |
| `edge_tts/cli/` | CLI argument parsing (app-layer) |

---

## API contract

### Object lifetime

`SpeechSynthesizer` construction is **cheap**: the full networking stack is
assembled but no connection is opened.  All network I/O is deferred to the
first call of `synthesize()` or `save()`.

```cpp
// Cheap — no network I/O here.
edge_tts::api::SpeechSynthesizer s("Hello world", cfg, opts);

// Network I/O happens here (connects to speech.platform.bing.com).
auto result = s.save("hello.mp3");
```

The object may be destroyed at any time after `synthesize()` or `save()`
returns (success or failure).  Do not destroy a `SpeechSynthesizer` while a
call to `synthesize()` or `save()` is executing on another thread.

### Single-use behavior

`synthesize()` and `save()` each **consume** the object.  A second call to
either returns `ErrorCode::invalid_state` regardless of which was called first.

```cpp
auto r1 = s.synthesize();   // OK
auto r2 = s.synthesize();   // returns ErrorCode::invalid_state
auto r3 = s.save("out.mp3");// also returns ErrorCode::invalid_state
```

To synthesize the same text again, construct a new `SpeechSynthesizer`.

### Thread safety

`SpeechSynthesizer` is **not thread-safe**.  Do not call `synthesize()`,
`save()`, or any accessor concurrently on the same object.  Constructing
independent `SpeechSynthesizer` objects in different threads is safe — there is
no shared global state.

### Error model

No exceptions are thrown for recoverable failures.  All errors are returned as
`Result<T>::fail(Error{...})`.  Use `result.error().code()` to branch on error
categories:

| `ErrorCode` | Cause |
|-------------|-------|
| `invalid_argument` | Bad `TtsConfig` fields (bad voice name, rate, etc.) |
| `invalid_state` | `synthesize()` or `save()` called a second time |
| `network_error` | WebSocket connection failed |
| `service_error` | Remote service returned no audio or an error status |
| `timeout` | Connect or receive timeout exceeded |
| `io_error` | Local file write failed (for `save()`) |
| `unsupported` | `SynthesisOptions::proxy` is set (proxy not supported) |
| `parse_error` | Malformed JSON from voice-list endpoint |

```cpp
auto result = s.save("hello.mp3");
if (!result) {
    switch (result.error().code()) {
    case edge_tts::common::ErrorCode::network_error:
        // retry or report
        break;
    case edge_tts::common::ErrorCode::invalid_argument:
        // bad config — fix TtsConfig fields
        break;
    default:
        std::cerr << result.error().what() << '\n';
    }
}
```

For the complete error-code reference — including context field contracts and credential-redaction policy — see the [Error handling reference](HIGH_LEVEL_DESIGN.md#error-handling-reference) section in `docs/HIGH_LEVEL_DESIGN.md`.

### Known limitations

**Subtitle timing model:** Subtitle boundary offsets for multi-chunk text are normalized using metadata only: `max(offset_ticks + duration_ticks)` across all boundary events in each completed chunk is accumulated as the offset compensation for subsequent chunks.  No fixed bitrate is assumed.  If a chunk produces no boundary events it contributes zero to the compensation.

**Proxy:** `SynthesisOptions::proxy` is validated at the API layer but actively rejected at runtime (`ErrorCode::unsupported`).  The ixwebsocket backend has no CONNECT-tunnel proxy API.  See the [Proxy support](#proxy-support-unsupported) section below.

### Chunk ownership

`synthesize()` returns `Result<vector<TtsChunk>>`.  The caller **owns** the
returned vector — it has no references back into the `SpeechSynthesizer` and
safely outlives the synthesizer object.

```cpp
std::vector<edge_tts::core::TtsChunk> chunks;
{
    edge_tts::api::SpeechSynthesizer s("Hello", cfg);
    auto r = s.synthesize();
    if (r) chunks = std::move(*r);
}
// s is destroyed here; chunks remain valid.
for (const auto& chunk : chunks) {
    if (edge_tts::core::is_audio(chunk)) {
        const auto& audio = std::get<edge_tts::core::AudioChunk>(chunk);
        // audio.data is std::vector<std::byte> owned by chunks
    }
}
```

### Voice listing

```cpp
#include <edge_tts/edge_tts.hpp>

// No SynthesisOptions needed for a basic fetch.
auto result = edge_tts::api::list_voices();
if (!result) {
    std::cerr << result.error().what() << '\n';
    return 1;
}
for (const auto& v : *result) {
    std::cout << v.short_name << "  " << v.locale << '\n';
}
```

`list_voices()` makes a single HTTP GET request, parses the JSON response, and
returns a `Result<vector<Voice>>`.  It is not thread-safe; call it from a
single thread or create one call site per thread.

---

## Linkage mode

edge-tts-cpp is a **static-only library**.  All compiled modules
(`edge_tts_common`, `edge_tts_core`, `edge_tts_serialization`,
`edge_tts_subtitle`, `edge_tts_communication`, `edge_tts_api`) are created with
an explicit `STATIC` keyword in CMake's `add_library()`.

The CMake variable `BUILD_SHARED_LIBS` is **intentionally ignored** for
`edge_tts_*` targets.  If your parent project sets `BUILD_SHARED_LIBS=ON`
globally, edge-tts-cpp still produces static archives (`.a` / `.lib`).

### Why static only?

- ixwebsocket is bundled as a static archive; mixing static and shared boundaries
  across submodule boundaries is fragile and untested.
- No symbol-visibility (`__declspec(dllexport/dllimport)` / `EDGE_TTS_API`) macro
  infrastructure exists yet.
- Windows shared-library support has not been CI-tested.

Shared library support may be added in a future release.  Until then, the
supported and tested mode is static linkage only.

### Installed artifacts

The `Development` install component installs static archives:

```
<prefix>/lib/libedge_tts_common.a
<prefix>/lib/libedge_tts_core.a
<prefix>/lib/libedge_tts_serialization.a
<prefix>/lib/libedge_tts_subtitle.a
<prefix>/lib/libedge_tts_communication.a
<prefix>/lib/libedge_tts_api.a
<prefix>/lib/libixwebsocket.a          (when compiled with ixwebsocket)
```

Consumers link `edge_tts::tts` (INTERFACE target) which carries all of these as
transitive link dependencies automatically.

---

## Proxy support (unsupported)

The ixwebsocket backend has no client-side CONNECT-tunnel proxy API.

- Setting `SynthesisOptions::proxy` is accepted at parse time and validated at the CLI/API layer.
- Any call to `save()`, `synthesize()`, or `list_voices()` with a proxy set **returns
  `ErrorCode::unsupported` immediately**, before any transport call is made.
- The CLI propagates this as exit code 1.

If proxy support is required, either route traffic through a transparent proxy
at the OS/network level, or use a different WebSocket library that supports the
CONNECT tunnel.

---

## Option A: find_package after cmake --install

The recommended integration for installed packages or distribution packages
(vcpkg, Conan, Homebrew, apt, etc.).

### 1. Install edge-tts-cpp

```bash
cmake -S path/to/edge-tts-cpp -B build \
    -DEDGE_TTS_INSTALL=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

`EDGE_TTS_INSTALL` is `ON` by default when edge-tts-cpp is the top-level
project.

#### Library-only install (recommended for package maintainers)

To install headers, archives, and CMake package files only — without CLI apps:

```bash
# EDGE_TTS_INSTALL_LIBRARY defaults ON; EDGE_TTS_INSTALL_APPS defaults OFF.
cmake --install build --component Development
```

#### Apps install

```bash
cmake -S path/to/edge-tts-cpp -B build \
    -DEDGE_TTS_INSTALL=ON \
    -DEDGE_TTS_INSTALL_APPS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build --component Apps
```

`edge-playback` is included only when `EDGE_TTS_BUILD_PLAYBACK_APP=ON` (default
on Linux/macOS; unavailable on Windows).

#### Install options summary

| Option | Default | Component | Description |
|--------|---------|-----------|-------------|
| `EDGE_TTS_INSTALL` | `ON` (top-level) | — | Generate install rules |
| `EDGE_TTS_INSTALL_LIBRARY` | `ON` | `Development` | Headers, archives, CMake package files |
| `EDGE_TTS_INSTALL_APPS` | `OFF` | `Apps` | `edge-tts` / `edge-playback` CLI binaries |
| `EDGE_TTS_INSTALL_TEST_SUPPORT` | `OFF` | `TestSupport` | `Fake*` test-double headers |

### 2. Consume in your project

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES CXX)

find_package(edge_tts_cpp REQUIRED)

add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

```cpp
#include <edge_tts/edge_tts.hpp>

int main() {
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";

    edge_tts::api::SpeechSynthesizer c("Hello world", std::move(cfg), {});
    auto result = c.save("hello.mp3");
    return result ? 0 : 1;
}
```

### 3. Configure your project

```bash
cmake -S my_project -B my_build \
    -DCMAKE_PREFIX_PATH=/usr/local
cmake --build my_build
```

---

## Option B: add_subdirectory

For projects that vendor edge-tts-cpp as a git submodule or copy the source
tree directly.

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES CXX)

# Prevents edge-tts-cpp from installing its targets when building as a
# sub-project.  Also prevents add_subdirectory from shadowing the parent
# project's CMAKE_SOURCE_DIR.
set(EDGE_TTS_INSTALL OFF)

add_subdirectory(
    path/to/edge-tts-cpp
    ${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp
    EXCLUDE_FROM_ALL
)

add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

Initialize the submodules before configuring:

```bash
git submodule update --init --recursive path/to/edge-tts-cpp
# or let CMake download them:
cmake -S . -B build -DEDGE_TTS_FETCH_DEPS=ON
```

---

## Do not link internal sub-targets manually

Link **only** `edge_tts::tts`.  Do not list any of the following explicitly:

```cmake
# WRONG — consumers must not manage edge-tts-cpp's internal graph
target_link_libraries(my_app PRIVATE
    edge_tts::communication
    edge_tts::serialization
    ixwebsocket
    nlohmann_json::nlohmann_json
)

# CORRECT — the single public entry point carries everything
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

`edge_tts::tts` transitively provides:
- All required static archives (`common`, `core`, `serialization`, `subtitle`,
  `communication`, `api`)
- The `ixwebsocket` link dependency (via `$<LINK_ONLY:...>` so its internal
  headers and compile definitions do not pollute your build)
- The `cxx_std_20` compile feature
- Correct include directories for both build-tree and installed usage

Internal modules (`edge_tts::cli`, `edge_tts::media`) are intentionally not
exported and must never be linked by consumers.

---

## Available targets after find_package

| Target | Description | Use when |
|--------|-------------|----------|
| `edge_tts::tts` | **Recommended entry point.** INTERFACE target that links the full TTS API. | Almost always |
| `edge_tts::api` | Synthesis facade (`SpeechSynthesizer`, `FileWriter`). | When you need the API type names at compile time |
| `edge_tts::communication` | WebSocket/HTTP transport. | Advanced: custom session control |
| `edge_tts::serialization` | SSML building and protocol framing. | Advanced: custom protocol work |
| `edge_tts::subtitle` | SRT subtitle generation. | Advanced: subtitle-only consumers |
| `edge_tts::core` | Domain types (`TtsConfig`, `Voice`). | Advanced: type-only inclusion |
| `edge_tts::common` | `Result<T>`, error codes, utilities. | Advanced: error-handling only |

Targets that are **not** exported (internal to the CLI apps):

- `edge_tts::cli` — argument parsing for `edge-tts` / `edge-playback`
- `edge_tts::media` — ffmpeg process runner

---

## Package name and config file

The CMake package name is `edge_tts_cpp`.  The generated config file is:

```
<prefix>/lib/cmake/edge_tts_cpp/edge_tts_cpp-config.cmake
```

CMake locates it through `CMAKE_PREFIX_PATH` or the standard system search
paths.  The package is also findable without a prefix if the install location
is already in CMake's search list (e.g. `/usr/local` on Linux/macOS).

---

## Versioning and compatibility policy

### Current version

The project is at **0.1.0** (pre-1.0 alpha).  See
[`docs/RELEASE_READINESS.md`](RELEASE_READINESS.md) for the maturity matrix.

### Stability guarantee

| Version range | Policy |
|---------------|--------|
| `0.x.y` (current) | **No stability guarantee.** Each minor version (`0.x`) may break the public API without deprecation warnings. |
| `1.0.0` and later | Full semver: breaking changes bump the major version only. |

**Deprecations:** Before 1.0, symbols may be removed without a deprecation cycle.
After 1.0, deprecated symbols will carry `[[deprecated("use X instead")]]` for
at least one minor version before removal.

### CMake find_package version requests

The installed package ships `edge_tts_cpp-config-version.cmake` generated with
`write_basic_package_version_file(...COMPATIBILITY SameMajorVersion)`.

| Request | Result (0.1.0 installed) |
|---------|--------------------------|
| `find_package(edge_tts_cpp REQUIRED)` | Succeeds — no constraint |
| `find_package(edge_tts_cpp 0.1 REQUIRED)` | Succeeds — same major, installed ≥ requested |
| `find_package(edge_tts_cpp 0.1.0 EXACT REQUIRED)` | Succeeds — exact match |
| `find_package(edge_tts_cpp 0.2 REQUIRED)` | **Fails** — 0.1.0 < 0.2 |
| `find_package(edge_tts_cpp 1.0 REQUIRED)` | **Fails** — different major |
| `find_package(edge_tts_cpp 0.1.1 EXACT REQUIRED)` | **Fails** — exact mismatch |

### Version macros

After `#include <edge_tts/edge_tts.hpp>` (or `#include <edge_tts/version.hpp>`):

```cpp
// Preprocessor macros — usable in #if guards
EDGE_TTS_CPP_VERSION_MAJOR   // int, e.g. 0
EDGE_TTS_CPP_VERSION_MINOR   // int, e.g. 1
EDGE_TTS_CPP_VERSION_PATCH   // int, e.g. 0
EDGE_TTS_CPP_VERSION         // string literal, e.g. "0.1.0"

// C++17 inline constexpr — type-safe, no ODR concerns
edge_tts::version_major      // inline constexpr int
edge_tts::version_minor      // inline constexpr int
edge_tts::version_patch      // inline constexpr int
edge_tts::version_string     // inline constexpr const char*
```

Example version guard:

```cpp
#include <edge_tts/version.hpp>
#if EDGE_TTS_CPP_VERSION_MAJOR == 0 && EDGE_TTS_CPP_VERSION_MINOR >= 1
// Use 0.1+ API
#endif
```

### Requesting a version at configure time

```cmake
# Accept any 0.x (x >= 1):
find_package(edge_tts_cpp 0.1 CONFIG REQUIRED)

# Accept only exactly 0.1.0:
find_package(edge_tts_cpp 0.1.0 EXACT CONFIG REQUIRED)
```

### Source of truth

The authoritative version is the `VERSION` field in the root `CMakeLists.txt`
`project()` declaration.  `include/edge_tts/version.hpp` is generated from that
value at configure time; do not edit it directly.

---

## Transitive dependencies

edge-tts-cpp pulls in the following dependencies automatically when consumed
via `find_package`:

| Dependency | Required | Notes |
|------------|----------|-------|
| `ixwebsocket` | Yes (when compiled with real networking) | Installed alongside edge-tts-cpp; no separate installation needed if you use the official release archive |
| `ZLIB` | Yes (via ixwebsocket TLS+gzip) | Resolved by the package config via `find_dependency(ZLIB)` |
| `Threads` | Yes (via ixwebsocket) | Resolved by `find_dependency(Threads)` |
| `nlohmann/json` | No | Header-only; used only in implementation files; not propagated to consumers |

---

## Relocation

The installed package is fully relocatable.  The generated CMake files use
`_IMPORT_PREFIX` computed relative to the config file's location, not the
build-time prefix.  You can copy or move the install tree to a different path
and `find_package` will continue to work.

---

## Consumer examples

Two ready-to-copy example projects live in the repository:

| Directory | Integration mode | What it shows |
|-----------|-----------------|---------------|
| [`examples/consumer_add_subdirectory/`](../examples/consumer_add_subdirectory/) | `add_subdirectory` | Vendored submodule layout, build-option overrides, `edge_tts::tts` link |
| [`examples/consumer_find_package/`](../examples/consumer_find_package/) | `find_package` | Install-then-consume pattern, `CMAKE_PREFIX_PATH`, `edge_tts::tts` link |

Both examples use `<edge_tts/edge_tts.hpp>` and link only `edge_tts::tts`.
Both are automatically built and verified by CTest (`edge_tts_consumer_examples_tests`).

A minimal test fixture (`tests/cmake/consumer_install_basic/`) is also used by
the install tree test (`edge_tts_install_tree_tests`) which installs edge-tts-cpp
to a temp prefix, configures and builds the fixture against it, then relocates the
install tree and re-builds to verify portability.
