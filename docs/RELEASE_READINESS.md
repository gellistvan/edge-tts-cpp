# Release Readiness

This document tracks the maturity of the `edge-tts-cpp` project and defines
what "ready to release" means for each capability area.

---

## Maturity model

| Label | Meaning |
|-------|---------|
| **Implemented** | Feature exists, has tests, matches the Python reference. |
| **Tested offline** | Deterministic tests run in the default `ctest` suite with no live service. |
| **Tested live** | Network tests exist and pass under `EDGE_TTS_ENABLE_NETWORK_TESTS=ON`. |
| **Integration-ready** | Public API is stable; breaking changes require a semver bump. |
| **Not started** | Not yet implemented. |

---

## Capability matrix

| Capability | Status | Notes |
|------------|--------|-------|
| Text-to-speech synthesis (`Communicate`) | **Implemented · Tested offline** | `api::Communicate` mirrors Python `Communicate`. Offline tests in `edge_tts_api_tests`. |
| Streaming audio chunks (`stream_sync`) | **Implemented · Tested offline** | |
| Save to file (`save`) | **Implemented · Tested offline** | MP3 + SRT written atomically. |
| Subtitle generation (SRT) | **Implemented · Tested offline** | `SubMaker` + `SrtComposer` match Python reference output. |
| Voice list (`VoiceService`) | **Implemented · Tested offline** | Fetches and parses Edge TTS voice list; client-side filtering. |
| Voice list filtering | **Implemented · Tested offline** | `VoiceFilter` supports locale, gender, short_name. |
| DRM token (`Sec-MS-GEC`) | **Implemented · Tested offline** | `EdgeTokenProvider` — SHA-256, 5-minute rounding, Windows epoch. |
| DRM 403 retry | **Implemented · Tested offline** | One retry with clock-skew correction, matching Python behavior. |
| Multi-chunk long text | **Implemented · Tested offline** | `TextChunker` splits at UTF-8 boundaries, 4096-byte limit. |
| `edge-tts` CLI | **Implemented · Tested offline** | Matches Python `edge-tts` option surface (see `docs/CLI_COMPATIBILITY.md`). |
| `edge-playback` CLI | **Implemented · Tested offline** | Temp-file lifecycle, proxy forwarding, error on unsupported options. |
| Proxy support | **Implemented** | `CommunicateOptions::proxy` forwarded through WebSocket connect. |
| ffmpeg integration | **Implemented · Tested offline** | `FfmpegAudioConverter` via injected `IProcessRunner`; real invocations not run in CI. |
| Windows build | **Partial** | CMake guards POSIX `ProcessRunner`; `FakeProcessRunner` and protocol code compile. Full Windows CI not yet in place. |
| Live network tests | **Tested live** | Gated behind `EDGE_TTS_ENABLE_NETWORK_TESTS=ON`; not run in default CI. |

---

## Code quality checklist

| Item | Status |
|------|--------|
| No skeleton placeholder strings in production source | **Done** — enforced by hygiene test |
| No `class Fake*` definitions in non-Fake production headers | **Done** — enforced by hygiene test (`FakeProcessRunner` in its own `FakeProcessRunner.hpp`) |
| No fake client headers included from production source | **Done** — enforced by hygiene test |
| Removed skeleton files absent from disk | **Done** — enforced by hygiene test |
| No build artifacts committed to git | **Done** — enforced by hygiene test |
| Module boundary violations | **None** — enforced by `check_module_boundaries.py` |
| Duplicate helper functions in tests | **Resolved** — shared `WebSocketFrameHelpers.hpp` |
| Stale "Stub:" labels in conditional-compile paths | **Resolved** — relabelled as "ixwebsocket not available" |
| `tools/README.md` placeholder text | **Resolved** — describes actual tools |
| All C++ tests pass | **Yes** — 17/17 CTest targets |
| All Python quality-gate tests pass | **Yes** — 4/4 gates (docs, module-boundary, dependency-config, hygiene) |

---

## Pre-release checklist

Before tagging a release:

1. `ctest --test-dir build --output-on-failure` — all 17 targets pass.
2. `python3 tests/tools/test_repository_hygiene.py` — all 7 hygiene checks pass.
3. `ctest --test-dir build -R "edge_tts_module_boundary_tests|edge_tts_docs_tests|edge_tts_dependency_config_tests" --output-on-failure` — all pass.
4. Build with `EDGE_TTS_WARNINGS_AS_ERRORS=ON` — zero warnings.
5. Enable `EDGE_TTS_ENABLE_NETWORK_TESTS=ON` in an environment with outbound TLS to `speech.platform.bing.com` — network smoke tests pass.
6. Run `tools/make_release_archive.sh <VERSION>` and verify the archive builds offline (see `docs/RELEASE.md`).
7. Update version in `CMakeLists.txt` `project()` call.
8. Tag: `git tag -a v<VERSION> -m "Release v<VERSION>"`.

---

## Known limitations

- **Windows:** `ProcessRunner` (POSIX fork/exec) is excluded. `edge-playback` cannot spawn `ffplay` on Windows without a Win32 `CreateProcess` implementation. Everything else compiles.
- **Proxy:** Forwarded to WebSocket connect options; not tested with a real proxy server.
- **Rate limiting:** No client-side rate limiter; matches Python behavior (no limiter there either).
