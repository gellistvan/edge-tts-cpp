# CLI Behavior Specification

This document specifies the behavior of the `edge-tts` and `edge-playback`
CLI commands and records intentional deviations from the reference behavior.

**Status legend:**

| Symbol | Meaning |
|--------|---------|
| `exact` | Behavior matches the specification |
| `partial` | Implemented but missing some behavior |
| `deviation` | Intentional difference; documented below |
| `unsupported` | Not implemented; attempting it returns a clear error |
| `N/A` | Not applicable to this command |

---

## `edge-tts`

**Argument parsing implementation:** `include/edge_tts/cli/EdgeTtsArgumentParser.hpp` /
`src/cli/EdgeTtsArgumentParser.cpp`.  `EdgeTtsArgumentParser::parse()` is stateless
and testable without synthesis, file I/O, or network calls.  It returns a
`ParseResult{action, arguments, message, exit_code}` — callers (main.cpp) handle
printing and `exit()`.

### Option matrix

| Option | Short | Argument | Default | Description | Status |
|--------|-------|----------|---------|-------------|--------|
| `--text` | `-t` | `STRING` | (required\*) | Text to synthesize. Mutually exclusive with `--file` and `--list-voices`. | `exact` |
| `--file` | `-f` | `PATH` | (required\*) | Read text from file; `-` or `/dev/stdin` reads stdin. Opens with UTF-8 encoding. Mutually exclusive with `--text` and `--list-voices`. | `exact` |
| `--list-voices` | `-l` | (flag) | (required\*) | Fetch voice list from service, print a tab-aligned table (Name, Gender, ContentCategories, VoicePersonalities), sorted by `ShortName`, then exit 0. Mutually exclusive with `--text` and `--file`. | `exact` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Voice name (short or full form). Validated via `TtsConfig::validate()`. | `exact` |
| `--rate` | — | `RATE` | `+0%` | Speech rate. Must match `^[+-]\d+%$`. Negative values require `--rate=-50%` syntax (not `--rate -50%`). | `exact` |
| `--volume` | — | `VOL` | `+0%` | Speech volume. Must match `^[+-]\d+%$`. Same negative-value syntax caveat as `--rate`. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Speech pitch. Must match `^[+-]\d+Hz$`. Same negative-value syntax caveat. | `exact` |
| `--write-media` | — | `PATH` | (none → stdout) | Write MP3 audio to `PATH`. If omitted, audio bytes go to `stdout`. `-` explicitly selects stdout. | `exact` |
| `--write-subtitles` | — | `PATH` | (none → no subtitles) | Write SRT subtitles to `PATH`. If omitted, no subtitles are written. `-` sends subtitles to **stderr**. | `exact` |
| `--proxy` | — | `URL` | (none) | HTTP/HTTPS proxy URL. Parsed and format-validated (`URL` must be non-empty and contain `://`; invalid format → exit 2). Flows into `api::SynthesisOptions::proxy`. **Runtime**: rejected at the API layer before any transport call; returns `ErrorCode::unsupported` (exit 1). **Security**: proxy URL credentials (`user:pass@`) are redacted to `[credentials]` in all CLI stderr output. | `deviation` (proxy not functional; returns explicit error) |
| `--version` | — | (flag) | — | Print `edge-tts-cpp {semver}` to stdout and exit 0. | `deviation` (version string differs from reference) |
| `--help` | `-h` | (flag) | — | Print help to stdout and exit 0. | `exact` |

\* `--text`, `--file`, and `--list-voices` form a **mutually exclusive required group** — exactly one must be provided.

### Behavioral notes

| # | Behavior | Status |
|---|----------|--------|
| 1 | **Interactive TTY warning.** If both stdin and stdout are TTYs and `--write-media` is not given, print a warning to stderr and wait for Enter before proceeding. If stdin reaches EOF (Ctrl-C or closed pipe), print "Operation canceled." to stderr and exit 0. | `exact` — `EdgeTtsCommandDispatcher::TtyCheckFn` is injectable; production `main.cpp` passes `isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)`. Full SIGINT handling is a known deviation (EOF path used instead). |
| 2 | **Audio default is stdout.** When `--write-media` is omitted, raw MP3 bytes are written to `stdout` (not a file). | `exact` |
| 3 | **Subtitle default is no output.** When `--write-subtitles` is omitted, no SRT is written anywhere. `-` sends it to stderr. | `exact` |
| 4 | **File input encoding.** `--file` reads the file with UTF-8 encoding. `-` and `/dev/stdin` are treated as stdin. | `exact` — `InputLoader` handles all three cases |
| 5 | **No `--format` option.** The audio format is hardcoded to `audio-24khz-48kbitrate-mono-mp3` in the WebSocket `speech.config` message. There is no CLI flag to change it. | `exact` (no format flag) |
| 6 | **No custom SSML.** The service only permits the single `<voice><prosody>` structure that the library already generates. | `exact` (no custom-SSML flag) |
| 7 | **Exit code 0 on success,** non-zero on error. `--list-voices` exits 0 after printing. | `exact` — exit 0 success, exit 1 runtime error, exit 2 invalid argument |
| 8 | **Negative-value syntax.** `--rate=-50%` works; `--rate -50%` is misinterpreted as an unknown option token. Users must use `=`-form for negative values. | `exact` (same constraint) |
| 9 | **Voice list sort order.** Voices are sorted ascending by `ShortName` before display. | `exact` |
| 10 | **Voice list columns.** `Name`, `Gender`, `ContentCategories` (comma-joined), `VoicePersonalities` (comma-joined). Formatted as a tab-aligned table. | `exact` — `VoiceFormatter` produces tabulate "simple" format: left-aligned columns padded to max width, separated by two spaces, dash separator row |
| 11 | **Empty text accepted at parse time.** `--text ""` passes argument parsing. The synthesizer produces no audio chunks for an empty string and exits 0 with no output written. | `exact` — empty string produces no audio; exit 0 |
| 12 | **File-not-found error includes the path.** When `--file` names a path that does not exist or cannot be read, the error message on stderr contains the supplied path. Exit code 1. | `exact` — `InputLoader` and dispatcher include the path in all `io_error` messages |
| 13 | **Invalid voice/rate/pitch/volume is a runtime error (exit 1), not a parse error (exit 2).** The argument parser accepts any non-empty string without format validation; values are forwarded to the synthesizer. Service-rejected values produce exit 1. | `exact` — no parse-time regex validation |
| 14 | **Output overwrite: existing file is silently overwritten.** When `--write-media` or `--write-subtitles` names an existing file, the file is overwritten without confirmation. | `deviation` (documented explicitly here; otherwise equivalent) |
| 15 | **Writing to a directory path fails.** If `--write-media` (or `--write-subtitles`) points to an existing directory, the write fails with an `io_error` message on stderr that includes the path. Exit code 1. `InputLoader` explicitly checks `is_directory()` before reading (on Linux `std::ifstream` opens a directory without error but fails on read). | `exact` — `InputLoader` directory guard, OS-level write failures propagate as `io_error` with path context |

---

## `edge-playback`

**Argument parsing implementation:** `include/edge_tts/cli/PlaybackArguments.hpp` /
`src/cli/PlaybackArgumentParser.cpp`.  `PlaybackArgumentParser::parse()` is stateless
and testable.  Dispatch: `include/edge_tts/cli/PlaybackCommandDispatcher.hpp` /
`src/cli/PlaybackCommandDispatcher.cpp`.  Entry point: `apps/edge-playback/main.cpp`.

**Implementation note:** The C++ implementation calls `api::SpeechSynthesizer::save()`
directly and `media::FfmpegAudioConverter::play_mp3()` for playback via `ffplay`,
rather than spawning subprocesses. Observable behavior (temp file lifecycle, env vars,
exit codes) is equivalent.

### Option matrix

| Option | Short | Argument | Default | Description | Status |
|--------|-------|----------|---------|-------------|--------|
| `--mpv` | — | (flag) | N/A | Explicitly rejected: returns exit 1 with a clear message ("only ffplay is available"). | `deviation` (see note 4) |
| `--text` | `-t` | `STRING` | (required\*) | Text to synthesize. | `exact` |
| `--file` | `-f` | `PATH` | (required\*) | Read text from file; same stdin/file semantics as `edge-tts`. | `exact` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Voice name. | `exact` |
| `--rate` | — | `RATE` | `+0%` | Speech rate. | `exact` |
| `--volume` | — | `VOL` | `+0%` | Speech volume. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Speech pitch. | `exact` |
| `--proxy` | — | `URL` | (none) | Proxy URL. Format-validated at parse time; rejected at runtime (exit 1). Credentials redacted from error output. | `deviation` (proxy not functional; returns explicit error) |
| `--write-media` | — | `PATH` | N/A | **Not accepted.** Returns parse error. | `N/A` |
| `--write-subtitles` | — | `PATH` | N/A | **Not accepted.** Returns parse error. | `N/A` |
| `--list-voices` | `-l` | (flag) | N/A | **Not accepted.** Returns parse error. | `N/A` |
| `--help` | `-h` | (flag) | — | Prints help. | `exact` |

\* `--text` and `--file` are mutually exclusive; exactly one must be provided.

### Behavioral notes

| # | Behavior | Status |
|---|----------|--------|
| 1 | **Library call model.** `api::SpeechSynthesizer::save()` is called directly; no subprocess spawning. Observable behavior is equivalent. | `deviation` (library call, not subprocess) |
| 2 | **Temp file lifecycle.** Temp `.mp3` (and `.srt` if `EDGE_PLAYBACK_SRT_FILE` is set) are created in the OS temp dir and deleted on exit unless `EDGE_PLAYBACK_KEEP_TEMP` is set. Cleanup happens even on synthesis or playback errors (RAII guard covers both files). | `exact` |
| 3 | **Playback.** Uses `ffplay -nodisp -autoexit <mp3>` via `FfmpegAudioConverter`. | `deviation` (ffplay instead of mpv) |
| 4 | **`--mpv` flag.** Passing `--mpv` produces `error: --mpv is not supported; only ffplay is available. Remove --mpv to use ffplay.` and exits 1. | `deviation` (explicit rejection) |
| 5 | **Windows playback.** `ProcessRunner.cpp` is excluded from the Windows build. Setting `EDGE_TTS_BUILD_PLAYBACK_APP=ON` on Windows is a configure-time `FATAL_ERROR`. The core library and `edge-tts` CLI build cleanly on Windows. | `unsupported` |
| 6 | **Dependency check.** `FfmpegAudioConverter` returns `external_process_failed` when ffplay is not on PATH, printed to stderr with exit 1. | `exact` (error at playback time) |
| 7 | **Negative-value syntax.** `--rate=-50%` works; `--rate -50%` is rejected with exit 2 because `-50%` is tokenized as an unknown option flag. Help text documents the `=`-form requirement. | `exact` (same constraint) |
| 8 | **File-not-found error includes the path.** When `--file` names a path that cannot be read, the error message on stderr contains the supplied path. Exit code 1. | `exact` |

### `edge-playback` environment variables

| Variable | Effect | C++ status |
|----------|--------|------------|
| `EDGE_PLAYBACK_DEBUG` | Print temp file paths to stdout before playback. | `exact` |
| `EDGE_PLAYBACK_KEEP_TEMP` | Keep temp files after playback exits. | `exact` |
| `EDGE_PLAYBACK_MP3_FILE` | Override the MP3 temp file path. | `exact` |
| `EDGE_PLAYBACK_SRT_FILE` | Override the SRT temp file path. When set, subtitles are synthesized and written to this path; cleaned up on exit unless `EDGE_PLAYBACK_KEEP_TEMP` is set. When unset, no SRT is generated. | `exact` |

### Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux / macOS (POSIX) | Supported | `ProcessRunner` uses `fork` / `execvp` / `pipe` / `waitpid`. `EDGE_TTS_BUILD_PLAYBACK_APP` defaults `ON`. |
| Windows | `unsupported` | `ProcessRunner.cpp` is excluded from the Windows build. Setting `EDGE_TTS_BUILD_PLAYBACK_APP=ON` on Windows is a configure-time `FATAL_ERROR` that names the platform. Build the core library and `edge-tts` CLI with `-DEDGE_TTS_BUILD_PLAYBACK_APP=OFF` (default on Windows) or provide a Windows-specific `IProcessRunner` implementation. |

### App installation policy

App binaries (`edge-tts`, `edge-playback`) are **not installed by default**. Most
library consumers only want headers and CMake package files.

| Scenario | CMake options |
|----------|---------------|
| Library only (default) | `EDGE_TTS_INSTALL_LIBRARY=ON` (default) |
| CLI tools only | `EDGE_TTS_INSTALL_LIBRARY=OFF -DEDGE_TTS_INSTALL_APPS=ON` |
| Everything | `EDGE_TTS_INSTALL_LIBRARY=ON -DEDGE_TTS_INSTALL_APPS=ON` |
| Component-based | `cmake --install build --component Development` or `--component Apps` |

`edge-playback` is only installed when **both** `EDGE_TTS_INSTALL_APPS=ON` and
`EDGE_TTS_BUILD_PLAYBACK_APP=ON` (POSIX only). It is silently omitted on Windows
or when the target was not built.

---

## Intentional deviations

| # | Area | Behavior | Notes |
|---|------|----------|-------|
| 1 | `--version` output | Prints `edge-tts-cpp {semver}` and exits 0 | Version tracks the C++ release |
| 2 | Voice list formatting | Custom stream formatting | Produces the same column layout |
| 3 | `synthesize()` / `save()` | Natively synchronous — no thread pool | External behavior is equivalent |
| 4 | `edge-playback` | Calls `api::SpeechSynthesizer::save()` directly | No subprocess spawning |

---

## Options intentionally not implemented

The following options are not accepted at the CLI level:

- `--format` (audio format is fixed to `audio-24khz-48kbitrate-mono-mp3`)
- `--boundary-type` (configured via library API only)
- `--timeout` (configured via library API only)
- Custom SSML input

---

## Exit code reference

| Condition | Exit code |
|-----------|-----------|
| Success | 0 |
| `--list-voices` completes | 0 |
| `--help` or `--version` | 0 |
| Invalid argument | 2 |
| Runtime error (network, no audio) | 1 |
| Missing deps (`edge-playback`) | 1 |
| `Ctrl-C` during TTY warning | 0 (EOF on stdin) |
