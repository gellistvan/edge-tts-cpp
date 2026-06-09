# Consuming edge-tts-cpp as a Library

This document describes the supported ways to integrate edge-tts-cpp into a
CMake project.

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

edge_tts::api::Communicate tts("Hello, world!", std::move(cfg), {});
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
| `edge_tts::api::Communicate` | Synthesize text to audio |
| `edge_tts::api::CommunicateOptions` | Transport options (timeouts) |
| `edge_tts::api::FileWriter` | Write audio/subtitle files |
| `edge_tts::core::TtsConfig` | Voice, rate, volume, pitch |
| `edge_tts::core::Voice` | Voice listing type |
| `edge_tts::core::AudioChunk` | Raw MP3 bytes from `stream_sync()` |
| `edge_tts::core::BoundaryChunk` | Word/sentence boundary event |
| `edge_tts::core::is_audio(chunk)` | `true` for `AudioChunk` variants |
| `edge_tts::common::Result<T>` | Error propagation (no exceptions for runtime errors) |
| `edge_tts::common::ErrorCode` | Error category enum |

Individual headers remain available for finer-grained includes:
`edge_tts/api/Communicate.hpp`, `edge_tts/core/TtsConfig.hpp`, etc.

---

## Proxy support (unsupported)

The ixwebsocket backend has no client-side CONNECT-tunnel proxy API.

- Setting `CommunicateOptions::proxy` is **validated** at the API layer.
- Any call to `save()` or `stream_sync()` with a proxy set **returns
  `ErrorCode::unsupported` immediately** — no network connection is attempted.
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

    edge_tts::api::Communicate c("Hello world", std::move(cfg), {});
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
| `edge_tts::api` | Synthesis facade (`Communicate`, `FileWriter`). | When you need the API type names at compile time |
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

## Version compatibility

The installed package exports `edge_tts_cppConfigVersion.cmake` generated with
`SameMajorVersion` compatibility.  This means:

- `find_package(edge_tts_cpp 0.1 REQUIRED)` — succeeds if any `0.x` is installed.
- `find_package(edge_tts_cpp 1.0 REQUIRED)` — fails if only `0.x` is installed.

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
