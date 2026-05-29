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

## Planned / Not Yet Integrated

| Library | Submodule path | Purpose | Status |
|---------|----------------|---------|--------|
| CLI11 | `submodules/CLI11` | Command-line argument parsing for `edge-tts` / `edge-playback` | Not yet added |
| ixwebsocket | `submodules/ixwebsocket` | WebSocket client for `communication::WebSocketTransport` | Not yet added |
| googletest | `submodules/googletest` | Alternative test runner (optional, currently unused) | Conditional in `Dependencies.cmake` |
