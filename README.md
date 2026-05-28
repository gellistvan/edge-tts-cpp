# edge-tts-cpp

A modern C++20 skeleton for a Microsoft Edge TTS client inspired by the Python `edge-tts` project.

This repository is currently a buildable project skeleton. Real networking, Edge protocol parsing, voice listing, and `ffmpeg` playback are intentionally stubbed until implemented step by step.

## Goals

- Modern C++20/23 style.
- Modular architecture with explicit namespaces and CMake module targets.
- CMake-first build.
- Tests split by module.
- Lightweight dependency strategy using `submodules/`.
- Runtime `ffmpeg` integration later, without linking directly to FFmpeg libraries.

## Module layout

```text
include/edge_tts/
  common/          shared errors and cross-cutting primitives
  core/            pure domain types and rules
  serialization/   SSML, Edge protocol payloads, token metadata
  communication/   Communicate facade, HTTP/WebSocket transport boundary
  media/           ffmpeg/ffplay process integration boundary
  subtitles/       subtitle cues and SRT composition

src/
  core/
  serialization/
  communication/
  media/
  subtitles/

tests/
  common/
  core/
  serialization/
  communication/
  media/
  subtitles/
```

Namespaces match the module names:

```cpp
edge_tts::common
edge_tts::core
edge_tts::serialization
edge_tts::communication
edge_tts::media
edge_tts::subtitles
```

The aggregate target is:

```cmake
target_link_libraries(my_app PRIVATE edge_tts::edge_tts)
```

Individual module targets are also available inside the build:

```cmake
edge_tts::common
edge_tts::core
edge_tts::serialization
edge_tts::communication
edge_tts::media
edge_tts::subtitles
```

## CLI compatibility

The `edge-tts` and `edge-playback` commands are designed to match the Python
`edge-tts` v7.2.8 CLI exactly, with intentional differences explicitly documented.

See [`docs/CLI_COMPATIBILITY.md`](docs/CLI_COMPATIBILITY.md) for the full
option-by-option compatibility matrix.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_TTS_BUILD_APPS` | `ON` | Build the `edge-tts` and `edge-playback` CLI apps |
| `EDGE_TTS_BUILD_TESTS` | `ON` | Build per-module test suites |
| `EDGE_TTS_BUILD_EXAMPLES` | `OFF` | Build example programs |
| `EDGE_TTS_WARNINGS_AS_ERRORS` | `OFF` | Promote compiler warnings to errors |
| `EDGE_TTS_ENABLE_NETWORK_TESTS` | `OFF` | Enable tests that call the live Edge TTS service |
| `EDGE_TTS_ENABLE_SANITIZERS` | `OFF` | Enable address and UB sanitizers |
| `EDGE_TTS_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy on compiled sources |

Example — strict developer build:

```bash
cmake -S . -B build \
    -DEDGE_TTS_BUILD_APPS=ON \
    -DEDGE_TTS_BUILD_TESTS=ON \
    -DEDGE_TTS_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

See [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) for the full development guide.

## Applications

The skeleton builds two placeholder apps:

```text
edge-tts
edge-playback
```

`edge-tts` will eventually expose options such as `--text`, `--file`, `--voice`, `--list-voices`, `--rate`, `--volume`, `--pitch`, `--write-media`, `--write-subtitles`, and `--proxy`.

## Tests

Tests are split into module-specific targets:

```text
edge_tts_common_tests
edge_tts_core_tests
edge_tts_serialization_tests
edge_tts_communication_tests
edge_tts_media_tests
edge_tts_subtitles_tests
```

Run everything with:

```bash
ctest --test-dir build --output-on-failure
```

## Dependency policy

Third-party dependencies should live in `submodules/` and be wired from `cmake/Dependencies.cmake`.

Planned dependencies:

- `googletest` for tests.
- `CLI11` for CLI parsing.
- `nlohmann/json` for voice list and protocol JSON.
- a lightweight WebSocket/HTTP dependency, likely `ixwebsocket`.

Boost is intentionally not required by this skeleton.

## FFmpeg policy

`ffmpeg` and optionally `ffplay` should be treated as runtime executables through the `edge_tts::media` module. The project should not link against FFmpeg libraries unless a future design decision explicitly changes that.

## Design documentation

See [`docs/high-level-design.md`](docs/high-level-design.md) for module boundaries, dependency direction, and testing structure.
