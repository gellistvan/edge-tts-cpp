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

## Build

```bash
cmake -S . -B build -DEDGE_TTS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

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
