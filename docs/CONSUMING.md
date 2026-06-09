# Consuming edge-tts-cpp as a Library

This document describes the supported ways to integrate edge-tts-cpp into a
CMake project.

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

## Minimal consumer example

A self-contained consumer fixture is provided at
`tests/cmake/consumer_install_basic/`.  The install tree test
(`tests/cmake/test_install_tree.py`, CTest name `edge_tts_install_tree_tests`)
configures, builds, installs edge-tts-cpp, and then:

1. Configures and **builds** the consumer fixture against the install prefix.
2. Copies the install prefix to a new location and **builds again** to verify
   relocation.
