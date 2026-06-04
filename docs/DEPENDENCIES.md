# Third-Party Dependencies

This document lists all third-party libraries used by `edge-tts-cpp`, their
source, how they are integrated, and which modules consume them.

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

---

## Planned / Not Yet Integrated

| Library | Submodule path | Purpose | Status |
|---------|----------------|---------|--------|
| CLI11 | `submodules/CLI11` | Command-line argument parsing for `edge-tts` / `edge-playback` | Not yet added |
| ixwebsocket | `submodules/ixwebsocket` | WebSocket client for `communication::WebSocketTransport` | Not yet added |
| googletest | `submodules/googletest` | Alternative test runner (optional, currently unused) | Conditional in `Dependencies.cmake` |
