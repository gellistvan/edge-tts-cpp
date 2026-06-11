# CLI Compatibility Matrix

Compatibility reference between the Python `edge-tts` v7.2.8 CLI and the
C++ `edge-tts-cpp` CLI.

**Sources inspected:**
- `reference/edge-tts/src/edge_tts/util.py` — argument parser and TTS runner for `edge-tts`
- `reference/edge-tts/src/edge_playback/__main__.py` — `edge-playback` entry point
- `reference/edge-tts/README.md` — usage examples and behavioral notes
- `reference/edge-tts/setup.cfg` — entry points and Python version requirement

**Status legend:**

| Symbol | Meaning |
|--------|---------|
| `exact` | C++ behavior is identical to Python |
| `partial` | Implemented but missing some behavior |
| `deviation` | Intentional difference from Python; documented below |
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

| Option | Short | Argument | Default | Python behavior | C++ behavior | Status |
|--------|-------|----------|---------|-----------------|----------------------|--------|
| `--text` | `-t` | `STRING` | (required\*) | Text to synthesize. Mutually exclusive with `--file` and `--list-voices`. | Identical. | `exact` |
| `--file` | `-f` | `PATH` | (required\*) | Read text from file; `-` or `/dev/stdin` reads stdin. Opens with UTF-8 encoding. Mutually exclusive with `--text` and `--list-voices`. | Identical. | `exact` |
| `--list-voices` | `-l` | (flag) | (required\*) | Fetch voice list from service, print a tab-aligned table (Name, Gender, ContentCategories, VoicePersonalities), sorted by `ShortName`, then `sys.exit(0)`. Mutually exclusive with `--text` and `--file`. | Identical table format and sort order. | `exact` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Voice name (short or full form). Normalized and validated at construction time. | Identical validation and normalization via `TtsConfig::validate()`. | `exact` |
| `--rate` | — | `RATE` | `+0%` | Speech rate. Must match `^[+-]\d+%$`. Negative values require `--rate=-50%` syntax (not `--rate -50%`) to avoid argparse misparse. | Identical pattern; same negative-value syntax note in help text. | `exact` |
| `--volume` | — | `VOL` | `+0%` | Speech volume. Must match `^[+-]\d+%$`. Same negative-value syntax caveat as `--rate`. | Identical. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Speech pitch. Must match `^[+-]\d+Hz$`. Same negative-value syntax caveat. | Identical. | `exact` |
| `--write-media` | — | `PATH` | (none → stdout) | Write MP3 audio to `PATH`. If omitted, audio bytes go to `stdout`. `-` explicitly selects stdout. | Identical; `-` → stdout, omitted → stdout. | `exact` |
| `--write-subtitles` | — | `PATH` | (none → no subtitles) | Write SRT subtitles to `PATH`. If omitted, no subtitles are written. `-` sends subtitles to **stderr**. | Identical; `-` → stderr, omitted → no SRT output. | `exact` |
| `--proxy` | — | `URL` | (none) | HTTP/HTTPS proxy URL. | Parsed and format-validated (`URL` must be non-empty and contain `://`; invalid format → exit 2). Flows into `api::SynthesisOptions::proxy`. **Runtime**: rejected at the API layer before any transport call; returns `ErrorCode::unsupported` (exit 1). **Security**: proxy URL credentials (`user:pass@`) are redacted to `[credentials]` in all CLI stderr output. | `deviation` (proxy not functional; returns explicit error) |
| `--version` | — | (flag) | — | Print `edge-tts {version}` (e.g. `edge-tts 7.2.8`) to stdout and exit 0. | Print `edge-tts-cpp {semver}` to stdout and exit 0. | `deviation` (version string differs) |
| `--help` | `-h` | (flag) | — | Print argparse-generated help to stdout and exit 0. | Identical behavior via hand-rolled parser. | `exact` |

\* `--text`, `--file`, and `--list-voices` form a **mutually exclusive required group** — exactly one must be provided.

### Behavioral notes

| # | Behavior | Python source | C++ status |
|---|----------|---------------|------------|
| 1 | **Interactive TTY warning.** If both stdin and stdout are TTYs and `--write-media` is not given, print a warning to stderr and wait for Enter before proceeding. If stdin reaches EOF (Ctrl-C or closed pipe), print "Operation canceled." to stderr and exit 0. | `util.py:_run_tts()` | `exact` — `EdgeTtsCommandDispatcher::TtyCheckFn` is injectable; production `main.cpp` passes `isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)`. Full SIGINT/KeyboardInterrupt handling is a known deviation (EOF path used instead). |
| 2 | **Audio default is stdout.** When `--write-media` is omitted, raw MP3 bytes are written to `stdout` (not a file). | `util.py:_run_tts()` | `exact` |
| 3 | **Subtitle default is no output.** When `--write-subtitles` is omitted, no SRT is written anywhere. `-` sends it to stderr. | `util.py:_run_tts()` | `exact` |
| 4 | **File input encoding.** `--file` reads the file with UTF-8 encoding. `-` and `/dev/stdin` are treated as stdin. | `util.py:amain()` | `exact` — `InputLoader` handles all three cases |
| 5 | **No `--format` option.** The audio format is hardcoded to `audio-24khz-48kbitrate-mono-mp3` in the WebSocket `speech.config` message. There is no CLI flag to change it. | `communicate.py`, `constants.py` | `exact` (no format flag) |
| 6 | **No custom SSML.** The Python project explicitly removed custom SSML support because the service only permits the single `<voice><prosody>` structure that the library already generates. | `README.md` | `exact` (no custom-SSML flag) |
| 7 | **Exit code 0 on success,** non-zero on error. `--list-voices` calls `sys.exit(0)` explicitly. | `util.py:amain()` | `exact` — exit 0 success, exit 1 runtime error, exit 2 invalid argument |
| 8 | **Negative-value syntax.** `--rate=-50%` works; `--rate -50%` is misinterpreted by argparse as an unknown option. The README documents this limitation explicitly. C++ parser has the same behavior; users must use `=`-form for negative values. | `README.md` | `exact` (same constraint) |
| 9 | **Voice list sort order.** Voices are sorted ascending by `ShortName` before display. | `util.py:_print_voices()` | `exact` |
| 10 | **Voice list columns.** `Name`, `Gender`, `ContentCategories` (comma-joined), `VoicePersonalities` (comma-joined). Formatted as a tab-aligned table using `tabulate`. | `util.py:_print_voices()` | `exact` — `VoiceFormatter` produces tabulate "simple" format: left-aligned columns padded to max width, separated by two spaces, dash separator row |
| 11 | **Empty text accepted at parse time.** `--text ""` passes argument parsing (exit 0 from parse). The synthesizer produces no audio chunks for an empty string and exits 0 with no output written. | (implicit) | `exact` — empty string produces no audio; exit 0 |
| 12 | **File-not-found error includes the path.** When `--file` names a path that does not exist or cannot be read, the error message printed to stderr contains the supplied path, making the error actionable without re-running with verbose flags. Exit code 1. | (implicit) | `exact` — `InputLoader` and dispatcher include the path in all `io_error` messages |
| 13 | **Invalid voice/rate/pitch/volume is a runtime error (exit 1), not a parse error (exit 2).** The argument parser accepts any non-empty string for `--voice`, `--rate`, `--pitch`, and `--volume` without format validation; strings are forwarded to the synthesizer. If the value is rejected by the service, the synthesizer returns `invalid_argument` which the dispatcher formats as a runtime error on stderr with exit 1. | (implicit) | `exact` — no parse-time regex validation; service-rejected values produce exit 1 |
| 14 | **Output overwrite: existing file is silently overwritten.** When `--write-media` or `--write-subtitles` names an existing file, the file is overwritten without confirmation. This is deliberate: piping or scripting scenarios should not be interrupted by interactive prompts. | (implicit) | `deviation` (Python also overwrites silently; documented explicitly here) |
| 15 | **Writing to a directory path fails.** If `--write-media` (or `--write-subtitles`) points to an existing directory instead of a file, the write fails with an `io_error` message on stderr that includes the path. Exit code 1. Similarly, passing a directory path to `--file` fails with `io_error` and exit 1. `InputLoader` explicitly checks `is_directory()` before attempting to read, because on Linux `std::ifstream` opens a directory descriptor successfully but throws `std::ios_failure` on the first read — the check converts this OS-specific behavior into a clean error. | (implicit) | `exact` — `InputLoader` directory guard, OS-level write failures propagate as `io_error` with path context |

---

## `edge-playback`

**Argument parsing implementation:** `include/edge_tts/cli/PlaybackArguments.hpp` /
`src/cli/PlaybackArgumentParser.cpp`.  `PlaybackArgumentParser::parse()` is stateless
and testable.  Dispatch: `include/edge_tts/cli/PlaybackCommandDispatcher.hpp` /
`src/cli/PlaybackCommandDispatcher.cpp`.  Entry point: `apps/edge-playback/main.cpp`.

**Implementation note (deviation from Python):** The Python reference calls
`edge-tts` as a subprocess (`_run_edge_tts()`).  The C++ implementation calls
`api::SpeechSynthesizer::save()` directly and `media::FfmpegAudioConverter::play_mp3()`
for playback via `ffplay`.  Observable behavior (temp file lifecycle, env vars,
exit codes) is identical.

### Option matrix

| Option | Short | Argument | Default | Python behavior | C++ behavior | Status |
|--------|-------|----------|---------|-----------------|--------------|--------|
| `--mpv` | — | (flag) | `false` on Windows, `true` elsewhere | Force mpv playback on Windows. On non-Windows mpv is always used. | Explicitly rejected: returns exit 1 with a clear message ("only ffplay is available"). | `deviation` (see note 4) |
| `--text` | `-t` | `STRING` | (required\*) | Forwarded verbatim to `edge-tts` subprocess. | Passed to `SpeechSynthesizer`. | `exact` |
| `--file` | `-f` | `PATH` | (required\*) | Forwarded verbatim to `edge-tts` subprocess. | Read via `InputLoader`; same stdin/file semantics. | `exact` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Forwarded verbatim to `edge-tts` subprocess. | Identical. | `exact` |
| `--rate` | — | `RATE` | `+0%` | Forwarded verbatim. | Identical. | `exact` |
| `--volume` | — | `VOL` | `+0%` | Forwarded verbatim. | Identical. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Forwarded verbatim. | Identical. | `exact` |
| `--proxy` | — | `URL` | (none) | HTTP/HTTPS proxy URL. | Format-validated at parse time (non-empty, must contain `://`); flows into `api::SynthesisOptions::proxy`. **Runtime**: rejected at the API layer before any transport call; returns `unsupported` (exit 1). **Security**: credentials redacted from error output as with `edge-tts`. | `deviation` (proxy not functional; returns explicit error) |
| `--write-media` | — | `PATH` | N/A | **Not accepted.** | Returns parse error. | `N/A` |
| `--write-subtitles` | — | `PATH` | N/A | **Not accepted.** | Returns parse error. | `N/A` |
| `--list-voices` | `-l` | (flag) | N/A | **Not accepted.** | Returns parse error. | `N/A` |
| `--help` | `-h` | (flag) | — | Prints minimal help. | Identical. | `exact` |

\* `--text` and `--file` are mutually exclusive; exactly one must be provided.

### Behavioral notes

| # | Behavior | Python source | C++ status |
|---|----------|---------------|------------|
| 1 | **Library call model.** C++ calls `api::SpeechSynthesizer::save()` directly instead of spawning an `edge-tts` subprocess. Observable behavior is equivalent. | `__main__.py:_run_edge_tts()` | `deviation` (library call, not subprocess) |
| 2 | **Temp file lifecycle.** Temp `.mp3` (and `.srt` if `EDGE_PLAYBACK_SRT_FILE` is set) are created in the OS temp dir and deleted on exit unless `EDGE_PLAYBACK_KEEP_TEMP` is set. Cleanup happens even on synthesis or playback errors (RAII guard covers both files). | `__main__.py:_cleanup()` | `exact` |
| 3 | **Playback.** Uses `ffplay -nodisp -autoexit <mp3>` via `FfmpegAudioConverter`. Python uses `mpv`. | `__main__.py:_play_media()` | `deviation` (ffplay instead of mpv) |
| 4 | **`--mpv` flag.** Python uses mpv by default on non-Windows; `--mpv` forces it on Windows. C++ build does not implement mpv: passing `--mpv` produces `error: --mpv is not supported; only ffplay is available. Remove --mpv to use ffplay.` and exits 1. | `__main__.py:_play_media()` | `deviation` (explicit rejection, not silent ignore) |
| 5 | **Windows playback.** `win32_playback.play_mp3_win32` when `--mpv` not set. | `__main__.py:_play_media()` | `unsupported` — `ProcessRunner.cpp` is excluded from the Windows build by CMake; setting `EDGE_TTS_BUILD_PLAYBACK_APP=ON` on Windows causes a configure-time `FATAL_ERROR` naming the platform. The core library and `edge-tts` CLI build cleanly on Windows. |
| 6 | **Dependency check.** `FfmpegAudioConverter` returns `external_process_failed` when ffplay is not on PATH, printed to stderr with exit 1. | `__main__.py:_check_deps()` | `exact` (error at playback time) |
| 7 | **Negative-value syntax.** Same constraint as `edge-tts` behavioral note 8: `--rate=-50%` works; `--rate -50%` is rejected with exit 2 because `-50%` is tokenized as an unknown option flag. Help text documents the `=`-form requirement. | (inherited from argparse behavior) | `exact` (same constraint) |
| 8 | **File-not-found error includes the path.** When `--file` names a path that cannot be read, the error message on stderr contains the supplied path. Exit code 1. | (implicit) | `exact` — dispatcher includes the path in all `io_error` messages |

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

## Intentional deviations from Python

| # | Area | Python behavior | C++ behavior | Reason |
|---|------|-----------------|--------------|--------|
| 1 | `--version` output | `edge-tts 7.2.8` | `edge-tts-cpp {semver}` | Different project; version tracks C++ release |
| 2 | Subtitle output format | `tabulate` library for voice list | Custom stream formatting | No Python dependency; must match column layout exactly |
| 3 | Async runtime | `asyncio.run()` wraps all I/O | Native C++ async or `std::thread` | Language difference; external behavior unchanged |
| 4 | Sync wrappers | `synthesize()` / `save()` are thin wrappers around async code, executed in a `ThreadPoolExecutor` | `synthesize()` and `save()` are natively synchronous — no thread pool is used | `deviation` (implementation strategy differs; external behavior is equivalent) |

---

## Options not present in Python that will NOT be added

The following are absent from the Python CLI and will remain absent from C++ to maintain compatibility:

- `--format` (audio format is fixed)
- `--boundary-type` at CLI level (configured via library API only)
- `--timeout` at CLI level (configured via library API only)
- Custom SSML input

---

## Exit code reference

| Condition | Python exit code | C++ exit code |
|-----------|------------------|---------------|
| Success | 0 | 0 |
| `--list-voices` completes | 0 (explicit `sys.exit(0)`) | 0 |
| `--help` or `--version` | 0 | 0 |
| Invalid argument | 2 (argparse default) | 2 |
| Runtime error (network, no audio) | 1 (unhandled exception) | 1 |
| Missing deps (`edge-playback`) | 1 (explicit `sys.exit(1)`) | 1 |
| `Ctrl-C` during TTY warning | 0 (explicit clean return) | 0 (EOF on stdin) |
