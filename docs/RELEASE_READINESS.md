# Release Readiness

**Overall project maturity: Alpha**

Core synthesis, voice listing, subtitles, and CLI are implemented and tested offline.
Proxy support is not yet functional end-to-end. Windows support is limited (synthesis works; `edge-playback` requires POSIX).
Live network tests are optional and not run in CI by default.

This document tracks the maturity of the `edge-tts-cpp` project and defines
what "ready to release" means for each capability area.

---

## Maturity model

| Label | Meaning |
|-------|---------|
| **Implemented** | Feature exists and has tests. |
| **Tested offline** | Deterministic tests run in the default `ctest` suite with no live service. |
| **Tested live** | Network tests exist and pass under `EDGE_TTS_ENABLE_NETWORK_TESTS=ON`. |
| **Integration-ready** | Public API is stable; breaking changes require a semver bump. |
| **Not started** | Not yet implemented. |

---

## Capability matrix

| Capability | Status | Notes |
|------------|--------|-------|
| Text-to-speech synthesis (`SpeechSynthesizer`) | **Implemented · Tested offline** | `api::SpeechSynthesizer`. Offline tests in `edge_tts_api_tests`. |
| Streaming audio chunks (`synthesize`) | **Implemented · Tested offline** | |
| Save to file (`save`) | **Implemented · Tested offline** | MP3 + SRT written atomically. |
| Subtitle generation (SRT) | **Implemented · Tested offline** | `SubMaker` + `SrtComposer`. |
| Voice list (`VoiceService`) | **Implemented · Tested offline** | Fetches and parses Edge TTS voice list; client-side filtering. |
| Voice list filtering | **Implemented · Tested offline** | `VoiceFilter` supports locale, gender, short_name. |
| DRM token (`Sec-MS-GEC`) | **Implemented · Tested offline** | `EdgeTokenProvider` — SHA-256, 5-minute rounding, Windows epoch. |
| DRM 403 retry | **Implemented · Tested offline** | One retry with clock-skew correction. |
| Multi-chunk long text | **Implemented · Tested offline** | `serialization::TextChunker` normalizes, XML-escapes, and splits at the 4096-byte escaped-length limit. |
| `edge-tts` CLI | **Implemented · Tested offline** | Matches Python `edge-tts` option surface (see `docs/CLI_COMPATIBILITY.md`). |
| `edge-playback` CLI | **Implemented · Tested offline** | Temp-file lifecycle, proxy forwarding, error on unsupported options. |
| Proxy support | **Partial** | `SynthesisOptions::proxy` is parsed and validated at the CLI/API layer; the ixwebsocket backend has no client-side proxy API and explicitly returns `ErrorCode::unsupported` if a proxy is set. Proxy is not functional end-to-end. |
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
| All C++ tests pass | **Yes** — 32 CTest targets |
| All Python quality-gate tests pass | **Yes** — 8/8 gates (docs, module-boundary, dependency-config, hygiene, cmake-source-dir-regression, consumer-add-subdirectory, public-tts-target, umbrella-header-hygiene) |
| Safe as add_subdirectory dependency | **Done** — EDGE_TTS_SOURCE_DIR/BINARY_DIR used throughout; regression check enforced by CTest |
| Public `edge_tts::tts` consumer target | **Done** — INTERFACE target linking `edge_tts::api`; does not expose CLI/playback/tests; consumer fixture and hygiene tests enforce it |
| Public umbrella header | **Done** — `include/edge_tts/edge_tts.hpp` aggregates stable consumer API (api, core, common); hygiene test enforces no cli/media/communication/serialization leakage; header self-containment and umbrella consumer tests in CTest |
| `cmake --install` + `find_package` support | **Done** — `EdgeTtsInstall.cmake` with `configure_package_config_file` / `write_basic_package_version_file`; consumer configure + build + relocation verified by `edge_tts_install_tree_tests` in CTest |
| Consumer examples | **Done** — `examples/consumer_add_subdirectory/` and `examples/consumer_find_package/` build and link correctly; verified by `edge_tts_consumer_examples_tests` in CTest |
| Package versioning | **Done** — `project(edge_tts_cpp VERSION 0.1.0)` is the single source of truth; `version.hpp` generated at configure time with `EDGE_TTS_CPP_VERSION_*` macros and `edge_tts::version_*` constexpr values; `SameMajorVersion` CMake compatibility; `edge_tts_package_version_tests` CTest validates macros, find_package version requests, and README consistency |
| Linkage mode | **Static-only (documented)** — all `edge_tts_*` modules use explicit `STATIC` in `add_library()`; `BUILD_SHARED_LIBS=ON` is ignored; `edge_tts_linkage_mode_tests` CTest verifies static artifacts, BUILD_SHARED_LIBS override behavior, and both add_subdirectory and find_package consumers; documented in `docs/CONSUMING.md` and `docs/DEPENDENCIES.md`; shared library support not provided (no symbol-visibility infrastructure, no Windows DLL CI) |
| Dependency-consumer readiness | **Done** — both add_subdirectory and find_package integration modes are fully tested; all 7 consumer user stories (submodule link, install+find_package, umbrella header, no-CLI synthesis, no-fakes-in-install, offline tests, proxy error) verified by `edge_tts_consumer_readiness_tests` CTest; dependency surface is clean (`edge_tts::tts` → `edge_tts::api` only, no cli/media/test-support leakage); consumer examples tested end-to-end; README has a dedicated "Use as a dependency" section |

---

## Pre-release checklist

Before tagging a release:

1. `ctest --test-dir build --output-on-failure` — all targets pass.
2. `python3 tests/tools/test_repository_hygiene.py` — all hygiene checks pass.
3. `ctest --test-dir build -R "edge_tts_module_boundary_tests|edge_tts_docs_tests|edge_tts_dependency_config_tests|edge_tts_cmake_source_dir_regression|edge_tts_consumer_add_subdirectory_tests|edge_tts_public_tts_target_tests|edge_tts_umbrella_header_hygiene_tests" --output-on-failure` — all pass.
4. Build with `EDGE_TTS_WARNINGS_AS_ERRORS=ON` — zero warnings.
5. Enable `EDGE_TTS_ENABLE_NETWORK_TESTS=ON` in an environment with outbound TLS to `speech.platform.bing.com` — network smoke tests pass.
6. Run `tools/make_release_archive.sh <VERSION>` and verify the archive builds offline (see `docs/RELEASE.md`).
7. Verify `add_subdirectory` integration: `python3 tests/cmake/test_consumer_add_subdirectory.py` passes with the release source tree.
7a. Verify public consumer target: `python3 tests/cmake/test_public_tts_target.py` passes — `edge_tts::tts` builds a consumer app without CLI/playback.
7b. Verify umbrella header: `python3 tests/cmake/test_umbrella_header_hygiene.py` passes — `edge_tts.hpp` includes required stable headers and excludes forbidden ones.
7c. Verify install tree: `python3 tests/cmake/test_install_tree.py` passes — install, find_package consumer build, and relocation all succeed.
7d. Verify consumer examples: `python3 tests/cmake/test_consumer_examples.py` passes — both `examples/consumer_add_subdirectory/` and `examples/consumer_find_package/` build cleanly.
7e. Verify package versioning: `python3 tests/cmake/test_package_version.py` passes — version.hpp macros match CMake version, find_package version constraints work, README mentions the version.
7f. Verify linkage mode: `python3 tests/cmake/test_linkage_mode.py` passes — static archives produced by default and with BUILD_SHARED_LIBS=ON; consumer add_subdirectory and find_package builds link correctly.
7g. Verify dependency-consumer readiness: `python3 tests/tools/test_consumer_readiness.py` passes — all 7 user stories covered, dependency surface clean, documentation cross-references valid, all test scripts registered.
8. Update version in `CMakeLists.txt` `project()` call and `README.md` version badge.
9. Tag: `git tag -a v<VERSION> -m "Release v<VERSION>"`.

---

## Known limitations

- **Windows:** `ProcessRunner` (POSIX fork/exec) is excluded. `edge-playback` cannot spawn `ffplay` on Windows without a Win32 `CreateProcess` implementation. Everything else compiles.
- **Proxy:** Recognized and validated at parse time, propagated into `SynthesisOptions::proxy`, but actively rejected at runtime by the ixwebsocket backend (`ErrorCode::unsupported`). The ixwebsocket library has no client-side CONNECT-tunnel proxy API. Any synthesis or voice-list call with a proxy set will fail with exit 1.
- **Subtitle timing approximation:** Subtitle boundary offsets for multi-chunk text are computed assuming a constant 48 kbps MP3 bitrate (`bytes × 8 × 10_000_000 / 48_000` ticks).  If the service sends audio at a different bitrate the cue timestamps for chunks after the first will drift.  Single-chunk text (< 4096 bytes escaped) is unaffected.
- **Rate limiting:** No client-side rate limiter; matches Python behavior (no limiter there either).
- **Static-only builds:** Shared library builds (`BUILD_SHARED_LIBS=ON`) are explicitly unsupported. All `edge_tts_*` modules are always compiled as static archives. No symbol-visibility (`EDGE_TTS_API`) infrastructure exists, and Windows DLL export/import has not been tested. `BUILD_SHARED_LIBS=ON` is silently ignored — static archives are produced regardless.
