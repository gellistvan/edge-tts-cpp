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

### Recommended: use a CMake preset

Four presets cover the common scenarios. Pick the one that matches your environment:

```bash
# Online developer build — auto-downloads missing deps via FetchContent
cmake --preset developer
cmake --build build
ctest --preset developer

# Offline with system packages — nlohmann-json3-dev and ixwebsocket must be installed
cmake --preset offline-system
cmake --build build

# Offline, no networking deps — only nlohmann-json needed, no CLI apps
cmake --preset offline-no-networking
cmake --build build

# Verify a release archive — configure must succeed without any download
cmake --preset archive-verify
cmake --build build-archive-verify
```

### Manual configure

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

**Dependency resolution:** submodules are the preferred source.  When a
submodule directory is empty (e.g. in a source archive), CMake falls back to
`find_package` (system install), then — only if `EDGE_TTS_FETCH_DEPS=ON` — to
`FetchContent`.  When all three sources fail, CMake aborts with a clear
configure-time error naming the missing package.  See `docs/DEPENDENCIES.md`
for the complete lookup-order policy.

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
| `EDGE_TTS_FETCH_DEPS` | `OFF` | Allow FetchContent to download missing dependencies. Set `ON` for the `developer` preset or any online CI. |
| `EDGE_TTS_REQUIRE_NETWORKING` | `ON` when `EDGE_TTS_BUILD_APPS=ON`, else `OFF` | Treat missing ixwebsocket as a fatal configure error |

Common manual configurations:

```bash
# Strict developer build (warnings as errors, auto-download deps)
cmake -S . -B build -DEDGE_TTS_FETCH_DEPS=ON -DEDGE_TTS_WARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build --output-on-failure

# Library + tests only (no apps, no ixwebsocket needed)
cmake -S . -B build -DEDGE_TTS_BUILD_APPS=OFF -DEDGE_TTS_REQUIRE_NETWORKING=OFF
cmake --build build

# Build a single module
cmake --build build --target edge_tts_core

# Run a single test target
cmake --build build --target edge_tts_core_tests
./build/tests/edge_tts_core_tests
```

### Building from a source archive

GitHub automatic "Source code" archives do **not** include submodule contents
(`submodules/json/` and `submodules/ixwebsocket/` are empty).  Use one of:

- **Official release archive** (`edge-tts-cpp-<VERSION>.tar.gz`): includes
  populated submodules — configure succeeds offline.
- **System packages**: `sudo apt install nlohmann-json3-dev` and then use the
  `offline-system` or `offline-no-networking` preset.
- **FetchContent**: add `-DEDGE_TTS_FETCH_DEPS=ON` to any cmake invocation;
  requires internet access at configure time.

See [`docs/RELEASE.md`](docs/RELEASE.md) for the complete source archive policy.

See [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) for the full development guide.

## Usage

```cpp
#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"  // transport options (proxy, timeouts)
#include "edge_tts/core/TtsConfig.hpp"

// Speech config — voice, rate, volume, pitch only (no transport settings).
edge_tts::core::TtsConfig cfg;
cfg.voice = "en-US-EmmaMultilingualNeural";

// Transport options — timeouts (no speech settings).
// NOTE: proxy is not supported by the ixwebsocket backend.
// Setting opts.proxy will cause stream_sync()/save() to return unsupported.
edge_tts::api::CommunicateOptions opts;

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

`edge-playback` synthesizes speech to a temp MP3, plays it through `ffplay`, and cleans up. Usage:

```bash
# Speak text
edge-playback --text "Hello, world!"

# Speak from a file
edge-playback --file speech.txt

# Keep the temp MP3 file after playback
EDGE_PLAYBACK_KEEP_TEMP=1 edge-playback --text "Hello"

# Write MP3 to a specific path (also keeps it)
EDGE_PLAYBACK_MP3_FILE=/tmp/hello.mp3 edge-playback --text "Hello"

# Write SRT subtitles alongside playback
EDGE_PLAYBACK_SRT_FILE=/tmp/hello.srt edge-playback --text "Hello"

# Show temp file paths (debug)
EDGE_PLAYBACK_DEBUG=1 edge-playback --text "Hello"
```

`edge-playback` options (subset of `edge-tts`; `--write-media`, `--write-subtitles`, `--list-voices` are not accepted):

| Option | Short | Default | Description |
|--------|-------|---------|-------------|
| `--text` | `-t` | (required\*) | Text to synthesize and play |
| `--file` | `-f` | (required\*) | Read text from file (`-` for stdin) |
| `--voice` | `-v` | `en-US-EmmaMultilingualNeural` | Voice name |
| `--rate` | — | `+0%` | Speech rate |
| `--volume` | — | `+0%` | Speech volume |
| `--pitch` | — | `+0Hz` | Speech pitch |
| `--proxy` | — | (none) | HTTP proxy URL (validated at parse time; **unsupported** at runtime — returns exit 1) |
| `--help` | `-h` | — | Print help and exit |

\* `--text` and `--file` are mutually exclusive; exactly one must be given.

**Playback backend:** `ffplay` (from FFmpeg) is used on all platforms. The `--mpv` flag is explicitly rejected with a clear error — passing it returns exit 1 with a message explaining the limitation.

**Platform support:** POSIX only (Linux, macOS). Building with `EDGE_TTS_BUILD_APPS=ON` on Windows will fail at compile time with a descriptive error from `ProcessRunner.cpp`.

Two CLI apps are built:

```text
edge-tts
edge-playback
```

`edge-tts` supports the following options (argument parsing is fully implemented via `EdgeTtsArgumentParser`):

| Option | Short | Default | Description |
|--------|-------|---------|-------------|
| `--text` | `-t` | (required\*) | Text to synthesize |
| `--file` | `-f` | (required\*) | Read text from file (`-` for stdin) |
| `--list-voices` | `-l` | (required\*) | Fetch voice list from the Edge TTS service and print a tab-aligned table, then exit 0 |
| `--voice` | `-v` | `en-US-EmmaMultilingualNeural` | Voice name |
| `--rate` | — | `+0%` | Speech rate (use `--rate=-50%` for negatives) |
| `--volume` | — | `+0%` | Speech volume |
| `--pitch` | — | `+0Hz` | Speech pitch |
| `--write-media` | — | (stdout) | Write MP3 to file |
| `--write-subtitles` | — | (none) | Write SRT to file (`-` for stderr) |
| `--proxy` | — | (none) | HTTP proxy URL (validated at parse time; **unsupported** at runtime — returns exit 1) |
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

### Real-network smoke tests

**WARNING: These tests contact Microsoft Edge TTS servers.  Do not run in CI without reliable outbound TLS access to `speech.platform.bing.com`.**

Two dedicated smoke-test files in `tests/network/` verify that the real Edge service still accepts the generated headers, DRM tokens, and protocol frames:

| File | Covers |
|------|--------|
| `tests/network/RealVoiceListTests.cpp` | HTTP 200, voice-list field completeness (ShortName, Gender, Locale), default voice present, locale/gender filters |
| `tests/network/RealSynthesisSmokeTests.cpp` | Short-phrase synthesis, non-empty audio, `turn.end` termination, word-boundary metadata, MP3/SRT file output, alternative voice accepted |

All network test targets carry CMake labels `network` and `integration`, enabling:

```bash
# Build (one-time, compile-time gate):
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
cmake --build build

# Run all real-network tests:
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -L network --output-on-failure

# Run only the dedicated smoke tests:
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build \
    -R edge_tts_network_smoke_tests --output-on-failure

# Verify skip behaviour (no env var — all pass via early return, no network calls):
./build/tests/edge_tts_network_smoke_tests
```

When `EDGE_TTS_RUN_NETWORK_TESTS` is unset every network test returns early without firing any assertion (minigtest has no SKIP; early-return tests show as PASSED).  Normal `ctest` never contacts the network.

### Offline integration coverage

`edge_tts_api_tests` includes deterministic end-to-end integration tests that exercise the complete path — `Communicate → SynthesisSession → EdgeProtocol → FakeWebSocketClient → FileWriter` — with no real network or live service required.  These tests always run in the default `ctest` suite and cover:

- **Frame structure**: verifies that `speech.config` and `ssml` frames have the correct `Path:` headers, `Content-Type`, and a 32-char hex `X-RequestId`.
- **Escaping**: "Tom & Jerry `<test>`" arrives in the SSML frame as `&amp;`/`&lt;`/`&gt;` — exactly once, never double-escaped.
- **Multi-chunk offset compensation**: long text split into two chunks; boundaries from chunk 2 are shifted by the audio duration of chunk 1, verified via both `stream_sync()` chunk values and `save()` SRT timestamps.
- **Error propagation**: unknown `Path` header → `protocol_error`; transport drop → `network_error`; no audio before `turn.end` → `service_error`.

See [`docs/TESTING.md`](docs/TESTING.md) for the full testing strategy and [`docs/HIGH_LEVEL_DESIGN.md`](docs/HIGH_LEVEL_DESIGN.md) for the tested data flow.

## Dependency policy

Third-party dependencies live in `submodules/` and are wired from `cmake/Dependencies.cmake`.

Integrated dependencies:

- `nlohmann/json` (`submodules/json`) — voice-list and protocol JSON parsing.
- `ixwebsocket` (`submodules/ixwebsocket`) — WebSocket + HTTP client for synthesis and voice listing.

Not used: `googletest` (replaced by `minigtest`, a self-contained single-header), `CLI11` (replaced by a hand-rolled parser that mirrors Python `argparse` behavior exactly).

Boost is intentionally not required.

## FFmpeg policy

`ffmpeg` and optionally `ffplay` should be treated as runtime executables through the `edge_tts::media` module. The project should not link against FFmpeg libraries unless a future design decision explicitly changes that.

## Design documentation

See [`docs/high-level-design.md`](docs/high-level-design.md) for module boundaries, dependency direction, and testing structure.
