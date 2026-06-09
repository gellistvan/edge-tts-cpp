# edge-tts-cpp

**Version: 0.1.0** (pre-1.0 — API may change between minor versions; see [Versioning](#versioning))

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

**Recommended consumer target: `edge_tts::tts`**

```cmake
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

`edge_tts::tts` is the stable, minimal public entry point for TTS consumers.
It links `edge_tts::api`, which transitively provides the full synthesis library
(`common`, `core`, `serialization`, `subtitle`, `communication`) without pulling
in CLI argument parsing, playback infrastructure, or test utilities.

**All available targets:**

| Target | Alias | Purpose | Stability |
|--------|-------|---------|-----------|
| `edge_tts_tts` | `edge_tts::tts` | **Recommended consumer target.** Links the TTS API and all transitive deps. No CLI, no playback, no tests. | Stable public API |
| `edge_tts_api` | `edge_tts::api` | Public synthesis facade (`Communicate`, `FileWriter`). | Stable public API |
| `edge_tts_communication` | `edge_tts::communication` | WebSocket/HTTP transport, DRM tokens, voice service. | Advanced use |
| `edge_tts_serialization` | `edge_tts::serialization` | SSML building, protocol framing, JSON parsing. | Advanced use |
| `edge_tts_subtitle` | `edge_tts::subtitle` | SRT subtitle generation. | Advanced use |
| `edge_tts_media` | `edge_tts::media` | ffplay/ffmpeg process runner. | Advanced use |
| `edge_tts_core` | `edge_tts::core` | Domain types (`TtsConfig`, `Voice`, `TtsChunk`). | Advanced use |
| `edge_tts_common` | `edge_tts::common` | Error types, `Result<T>`, `IClock`. | Advanced use |
| `edge_tts_cli` | `edge_tts::cli` | CLI argument parsing for `edge-tts` / `edge-playback`. | Internal / app-only |
| `edge_tts` | `edge_tts::edge_tts`, `edge_tts::all` | Broad aggregate: all modules including CLI. For internal examples only. | Internal convenience |

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
| `EDGE_TTS_BUILD_APPS` | `ON` | Build the `edge-tts` CLI app |
| `EDGE_TTS_BUILD_PLAYBACK_APP` | `ON` on Linux/macOS, `OFF` on Windows | Build the `edge-playback` CLI app (POSIX only; `ON` on Windows is a configure-time error) |
| `EDGE_TTS_BUILD_TESTS` | `ON` | Build per-module test suites |
| `EDGE_TTS_BUILD_EXAMPLES` | `OFF` | Build example programs |
| `EDGE_TTS_WARNINGS_AS_ERRORS` | `OFF` | Promote compiler warnings to errors |
| `EDGE_TTS_ENABLE_NETWORK_TESTS` | `OFF` | Enable tests that call the live Edge TTS service |
| `EDGE_TTS_ENABLE_SANITIZERS` | `OFF` | Enable address and UB sanitizers |
| `EDGE_TTS_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy on compiled sources |
| `EDGE_TTS_FETCH_DEPS` | `OFF` | Allow FetchContent to download missing dependencies. Set `ON` for the `developer` preset or any online CI. |
| `EDGE_TTS_REQUIRE_NETWORKING` | `ON` when any app is enabled, else `OFF` | Treat missing ixwebsocket as a fatal configure error |

### Platform support

| Platform | Core library | `edge-tts` CLI | `edge-playback` CLI |
|----------|-------------|----------------|---------------------|
| Linux | Supported | Supported | Supported (default ON) |
| macOS | Supported | Supported | Supported (default ON) |
| Windows | Supported | Supported | **Not supported** — `ProcessRunner` requires POSIX (`fork`/`execvp`/`pipe`/`waitpid`). `EDGE_TTS_BUILD_PLAYBACK_APP` defaults `OFF` on Windows; setting it `ON` is a configure-time fatal error. |

The core library (`common`, `core`, `serialization`, `communication`, `api`) and the `edge-tts` CLI have no POSIX process dependencies and build cleanly on Windows. Only `edge-playback` requires POSIX via `ProcessRunner`.

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

### Using as an add_subdirectory dependency

edge-tts-cpp can be consumed from a parent CMake project via `add_subdirectory`.
A ready-to-copy example is in [`examples/consumer_add_subdirectory/`](examples/consumer_add_subdirectory/).

```cmake
# In your parent CMakeLists.txt:
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES CXX)

# Disable install rules and apps when using as a sub-project:
set(EDGE_TTS_INSTALL    OFF CACHE BOOL "" FORCE)
set(EDGE_TTS_BUILD_APPS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    path/to/edge-tts-cpp
    ${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp
    EXCLUDE_FROM_ALL
)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE edge_tts::tts)  # recommended consumer target
```

edge-tts-cpp uses `EDGE_TTS_SOURCE_DIR` / `EDGE_TTS_BINARY_DIR` internally (set
to `CMAKE_CURRENT_SOURCE_DIR` / `CMAKE_CURRENT_BINARY_DIR` at its own root) so
your parent project's `CMAKE_SOURCE_DIR` is never touched.

The submodules (`submodules/json`, `submodules/ixwebsocket`) must be initialized
before the parent project configures:

```bash
git submodule update --init --recursive path/to/edge-tts-cpp
```

Or set `EDGE_TTS_FETCH_DEPS=ON` to let CMake download them automatically.

### Installing and using via find_package

edge-tts-cpp supports `cmake --install` to produce an install tree consumable
via `find_package(edge_tts_cpp CONFIG REQUIRED)`.
A ready-to-copy example is in [`examples/consumer_find_package/`](examples/consumer_find_package/).

**Install:**

```bash
cmake -S . -B build -DEDGE_TTS_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

`EDGE_TTS_INSTALL` defaults to `ON` when edge-tts-cpp is the top-level project
and `OFF` when consumed via `add_subdirectory`.

**Consume in your project:**

```cmake
find_package(edge_tts_cpp REQUIRED)
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

**Install options:**

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_TTS_INSTALL` | `ON` (top-level) | Generate `cmake --install` rules |
| `EDGE_TTS_INSTALL_LIBRARY` | `ON` | Install headers, archives, and CMake package files (`Development` component) |
| `EDGE_TTS_INSTALL_APPS` | `OFF` | Install `edge-tts` / `edge-playback` binaries (`Apps` component) |
| `EDGE_TTS_INSTALL_TEST_SUPPORT` | `OFF` | Install `Fake*` test-support headers (test-only) |

**Component-based install (selective):**

```bash
# Library only (headers + archives + CMake package files):
cmake --install build --component Development

# CLI apps only:
cmake --install build --component Apps
```

The installed tree layout follows GNUInstallDirs conventions:

```
<prefix>/include/edge_tts/     public headers
<prefix>/lib/libedge_tts_*.a   compiled modules
<prefix>/lib/cmake/edge_tts_cpp/
    edge_tts_cpp-config.cmake
    edge_tts_cpp-config-version.cmake
    edge_tts_cpp-targets.cmake
```

See [`docs/CONSUMING.md`](docs/CONSUMING.md) for the complete integration
guide including available targets, version compatibility, and transitive
dependency behavior.

## Usage

Include the umbrella header and link `edge_tts::tts`:

```cpp
#include <edge_tts/edge_tts.hpp>

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

```cmake
target_link_libraries(my_app PRIVATE edge_tts::tts)
```

Both `stream_sync()` and `save()` are single-use — a second call returns
`ErrorCode::invalid_state`, matching Python's `RuntimeError`.

Individual headers (`edge_tts/api/Communicate.hpp`, `edge_tts/core/TtsConfig.hpp`,
etc.) remain available for consumers who need finer-grained includes.

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

**Platform support:** POSIX only (Linux, macOS). `EDGE_TTS_BUILD_PLAYBACK_APP` defaults `OFF` on Windows; setting it `ON` on Windows is a configure-time `FATAL_ERROR`. The `edge-tts` CLI (synthesis only) builds cleanly on Windows.

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

## Versioning

The project follows [Semantic Versioning](https://semver.org) with these policies:

| Range | Compatibility |
|-------|---------------|
| `0.x.y` (current) | **No stability guarantee.** Minor version bumps (`0.x`) may break the public API. |
| `1.0.0` and later | Full semver: breaking changes only on major bumps. |

**Pre-1.0 policy:** The project is currently at `0.1.0`. Any minor version bump (`0.2`, `0.3`, …) may change or remove public API. Treat each `0.x` as a potentially breaking release. Once the API stabilizes, `1.0.0` will be tagged and the stable-API guarantee will apply.

**CMake package compatibility:** `SameMajorVersion`. This means:
- `find_package(edge_tts_cpp 0.1 REQUIRED)` — succeeds if `0.x` (x ≥ 1) is installed.
- `find_package(edge_tts_cpp 1.0 REQUIRED)` — fails if only `0.x` is installed (different major).

**Version macros (after `#include <edge_tts/edge_tts.hpp>`):**

```cpp
EDGE_TTS_CPP_VERSION_MAJOR  // int
EDGE_TTS_CPP_VERSION_MINOR  // int
EDGE_TTS_CPP_VERSION_PATCH  // int
EDGE_TTS_CPP_VERSION        // string literal, e.g. "0.1.0"

edge_tts::version_major     // inline constexpr int
edge_tts::version_minor     // inline constexpr int
edge_tts::version_patch     // inline constexpr int
edge_tts::version_string    // inline constexpr const char*
```

See [`docs/CONSUMING.md`](docs/CONSUMING.md#versioning-and-compatibility-policy) for the full policy.

## Design documentation

See [`docs/HIGH_LEVEL_DESIGN.md`](docs/HIGH_LEVEL_DESIGN.md) for module boundaries, dependency direction, and testing structure.

See [`docs/TESTING.md`](docs/TESTING.md) for the full testing strategy.

See [`docs/RELEASE_READINESS.md`](docs/RELEASE_READINESS.md) for the current maturity level and pre-release checklist.
