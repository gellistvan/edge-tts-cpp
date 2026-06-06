# edge-tts-cpp

A modern C++20 implementation of a Microsoft Edge TTS client, inspired by the Python `edge-tts` project.

Real networking (WebSocket + HTTP), Edge protocol parsing, DRM token generation, and voice listing are wired and functional. `ffmpeg`/`ffplay` playback integration is implemented via runtime process execution.

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
  common/          shared errors, Expected<T,E>, and UTF-8 utilities
  core/            pure domain types: TtsConfig, Voice, Chunk, TextChunker
  serialization/   SSML, Edge protocol payloads, token metadata
  communication/   Communicate facade, HTTP/WebSocket transport boundary
  media/           external process integration: IAudioConverter, FfmpegAudioConverter (ffmpeg/ffplay via IProcessRunner, no library linking), ExecutableDiscovery, ProcessRunner
  subtitles/       subtitle cues and SRT composition
  cli/             CLI argument parsing and dispatch (EdgeTtsArgumentParser, EdgeTtsArguments,
                   PlaybackArgumentParser, PlaybackArguments, PlaybackCommandDispatcher,
                   InputLoader, VoiceFormatter, EdgeTtsCommandDispatcher)

src/
  core/
  serialization/
  communication/
  media/
  subtitles/
  cli/

tests/
  common/
  core/
  serialization/
  communication/
  media/
  subtitles/
  cli/
```

### CMake targets

Each module is a separate CMake library.  Link specific targets — do not link
the aggregate unless writing examples.

| Module | Target | Alias | Test target |
|--------|--------|-------|-------------|
| common | `edge_tts_common` | `edge_tts::common` | `edge_tts_common_tests` |
| core | `edge_tts_core` | `edge_tts::core` | `edge_tts_core_tests` |
| serialization | `edge_tts_serialization` | `edge_tts::serialization` | `edge_tts_serialization_tests` |
| subtitle | `edge_tts_subtitle` | `edge_tts::subtitle` | `edge_tts_subtitle_tests` |
| media | `edge_tts_media` | `edge_tts::media` | `edge_tts_media_tests` |
| communication | `edge_tts_communication` | `edge_tts::communication` | `edge_tts_communication_tests` |
| api | `edge_tts_api` | `edge_tts::api` | `edge_tts_api_tests` |
| cli | `edge_tts_cli` | `edge_tts::cli` | `edge_tts_cli_tests` |

The aggregate convenience target:

```cmake
target_link_libraries(my_example PRIVATE edge_tts::edge_tts)
```

See [`docs/DEPENDENCY_RULES.md`](docs/DEPENDENCY_RULES.md) for the enforced
dependency matrix and [`docs/MODULES.md`](docs/MODULES.md) for per-module
ownership details.

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
| `EDGE_TTS_FETCH_DEPS` | `ON` | Allow FetchContent to download missing dependencies automatically when submodules are absent |
| `EDGE_TTS_REQUIRE_NETWORKING` | `ON` when `EDGE_TTS_BUILD_APPS=ON`, else `OFF` | Treat missing ixwebsocket as a fatal configure error (prevents silently building apps against stub networking) |

**Dependency resolution:** submodules are the preferred source.  When a
submodule directory is empty (e.g. in a source archive), CMake falls back to
`find_package` (system install) then `FetchContent` (auto-download, requires
`EDGE_TTS_FETCH_DEPS=ON`).  See `docs/DEPENDENCIES.md` for details.

Common build configurations:

```bash
# Default (apps + tests)
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

# Strict developer build (warnings as errors)
cmake -S . -B build -DEDGE_TTS_WARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build --output-on-failure

# Library + tests only (no apps)
cmake -S . -B build -DEDGE_TTS_BUILD_APPS=OFF
cmake --build build

# Build a single module
cmake --build build --target edge_tts_core

# Run a single test target
cmake --build build --target edge_tts_core_tests
./build/tests/edge_tts_core_tests
```

See [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) for the full development guide.

## Usage

```cpp
#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"  // transport options (proxy, timeouts)
#include "edge_tts/core/TtsConfig.hpp"

// Speech config — voice, rate, volume, pitch only (no transport settings).
edge_tts::core::TtsConfig cfg;
cfg.voice = "en-US-EmmaMultilingualNeural";

// Transport options — proxy and timeouts (no speech settings).
edge_tts::api::CommunicateOptions opts;
opts.proxy = "http://proxy.example.com:8080"; // optional

// Save audio and optional SRT subtitles (reference: Communicate.save()).
edge_tts::api::Communicate c("Hello, world!", std::move(cfg), std::move(opts));
auto result = c.save("hello.mp3", "hello.srt");
if (!result) {
    std::cerr << result.error().what() << '\n';
}

// Without a proxy — the 2-arg form uses default options.
edge_tts::api::Communicate c2("Hello again!");
auto chunks = c2.stream_sync();
if (chunks) {
    for (const auto& chunk : *chunks) {
        if (edge_tts::core::is_audio(chunk)) { /* write audio bytes */ }
        else                                  { /* process boundary event */ }
    }
}
```

Both `stream_sync()` and `save()` are single-use — a second call returns
`ErrorCode::invalid_state`, matching Python's `RuntimeError`.

Inject a `SynthesizerFn` for testing without a live service connection.

## Applications

The CLI is wired end-to-end via `EdgeTtsCommandDispatcher`, which routes parsed arguments to the right handler with injectable dependencies (voice service, Communicate factory, streams):

```
edge-tts --text "Hello" --write-media hello.mp3 --write-subtitles hello.srt
edge-tts --list-voices
edge-tts --help
edge-tts --version
```

The dispatcher exits 0 on success, 1 on runtime errors (service/synthesis/file I/O), and 2 on argument errors — matching Python `argparse` and `sys.exit()` behavior.

The skeleton builds two placeholder apps:

```text
edge-tts
edge-playback
```

`edge-tts` supports the following options (argument parsing is fully implemented via `EdgeTtsArgumentParser`):

| Option | Short | Default | Description |
|--------|-------|---------|-------------|
| `--text` | `-t` | (required\*) | Text to synthesize |
| `--file` | `-f` | (required\*) | Read text from file (`-` for stdin) |
| `--list-voices` | `-l` | (required\*) | List available voices and exit |
| `--voice` | `-v` | `en-US-EmmaMultilingualNeural` | Voice name |
| `--rate` | — | `+0%` | Speech rate (use `--rate=-50%` for negatives) |
| `--volume` | — | `+0%` | Speech volume |
| `--pitch` | — | `+0Hz` | Speech pitch |
| `--write-media` | — | (stdout) | Write MP3 to file |
| `--write-subtitles` | — | (none) | Write SRT to file (`-` for stderr) |
| `--proxy` | — | (none) | HTTP proxy for TTS and voice list |
| `--version` | — | — | Print version and exit |
| `--help` | `-h` | — | Print help and exit |

\* `--text`, `--file`, and `--list-voices` are mutually exclusive; exactly one must be given.

**Input loading** (`cli::InputLoader`) resolves text in the order the reference dictates:
1. `--text TEXT` → used verbatim
2. `--file -` or `--file /dev/stdin` → read from stdin
3. `--file PATH` → open UTF-8 file and read entirely (CRLF preserved on Linux)

## Tests

Tests are split into module-specific targets:

```text
edge_tts_common_tests
edge_tts_core_tests
edge_tts_serialization_tests
edge_tts_communication_tests
edge_tts_api_tests
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
