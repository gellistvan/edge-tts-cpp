# CLI Compatibility Matrix

Compatibility reference between the Python `edge-tts` v7.2.8 CLI and the
planned C++ `edge-tts-cpp` CLI.

**Sources inspected:**
- `reference/edge-tts/src/edge_tts/util.py` — argument parser and TTS runner for `edge-tts`
- `reference/edge-tts/src/edge_playback/__main__.py` — `edge-playback` entry point
- `reference/edge-tts/README.md` — usage examples and behavioral notes
- `reference/edge-tts/setup.cfg` — entry points and Python version requirement

**Status legend:**

| Symbol | Meaning |
|--------|---------|
| `exact` | C++ behavior must be identical to Python |
| `planned` | Intended to implement with identical behavior; not yet done |
| `partial` | Implemented but missing some behavior |
| `deviation` | Intentional difference from Python; documented below |
| `N/A` | Not applicable to this command |
| `stub` | Placeholder only; no behavior yet |

---

## `edge-tts`

**Argument parsing implementation:** `include/edge_tts/cli/EdgeTtsArgumentParser.hpp` /
`src/cli/EdgeTtsArgumentParser.cpp`.  `EdgeTtsArgumentParser::parse()` is stateless
and testable without synthesis, file I/O, or network calls.  It returns a
`ParseResult{action, arguments, message, exit_code}` — callers (main.cpp) handle
printing and `exit()`.

### Option matrix

| Option | Short | Argument | Default | Python behavior | C++ planned behavior | Status |
|--------|-------|----------|---------|-----------------|----------------------|--------|
| `--text` | `-t` | `STRING` | (required\*) | Text to synthesize. Mutually exclusive with `--file` and `--list-voices`. | Identical. | `exact` |
| `--file` | `-f` | `PATH` | (required\*) | Read text from file; `-` or `/dev/stdin` reads stdin. Opens with UTF-8 encoding. Mutually exclusive with `--text` and `--list-voices`. | Identical. | `partial` (parser done; file I/O in runner) |
| `--list-voices` | `-l` | (flag) | (required\*) | Fetch voice list from service, print a tab-aligned table (Name, Gender, ContentCategories, VoicePersonalities), sorted by `ShortName`, then `sys.exit(0)`. Mutually exclusive with `--text` and `--file`. | Identical table format and sort order. | `exact` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Voice name (short or full form). Normalized and validated at construction time. | Identical validation and normalization via `TtsConfig::validate()`. | `partial` (parser done; validation on TtsConfig construction) |
| `--rate` | — | `RATE` | `+0%` | Speech rate. Must match `^[+-]\d+%$`. Negative values require `--rate=-50%` syntax (not `--rate -50%`) to avoid argparse misparse. | Identical pattern; same negative-value syntax note in help text. | `exact` |
| `--volume` | — | `VOL` | `+0%` | Speech volume. Must match `^[+-]\d+%$`. Same negative-value syntax caveat as `--rate`. | Identical. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Speech pitch. Must match `^[+-]\d+Hz$`. Same negative-value syntax caveat. | Identical. | `exact` |
| `--write-media` | — | `PATH` | (none → stdout) | Write MP3 audio to `PATH`. If omitted, audio bytes go to `stdout`. `-` explicitly selects stdout. | Identical; `-` → stdout, omitted → stdout. | `exact` |
| `--write-subtitles` | — | `PATH` | (none → no subtitles) | Write SRT subtitles to `PATH`. If omitted, no subtitles are written. `-` sends subtitles to **stderr**. | Identical; `-` → stderr, omitted → no SRT output. | `exact` |
| `--proxy` | — | `URL` | (none) | HTTP/HTTPS proxy URL forwarded verbatim to the aiohttp WebSocket and voice-list clients. | Parsed into `EdgeTtsArguments::proxy`; `EdgeTtsCommandDispatcher` copies it into `api::CommunicateOptions::proxy`; production `Communicate` constructors forward it into `WebSocketClientOptions::proxy` for the real networking stack. | `exact` |
| `--version` | — | (flag) | — | Print `edge-tts {version}` (e.g. `edge-tts 7.2.8`) to stdout and exit 0. | Print `edge-tts-cpp {semver}` to stdout and exit 0. | `deviation` (version string differs) |
| `--help` | `-h` | (flag) | — | Print argparse-generated help to stdout and exit 0. | Identical behavior via hand-rolled parser. | `exact` |

\* `--text`, `--file`, and `--list-voices` form a **mutually exclusive required group** — exactly one must be provided.

### Behavioral notes

| # | Behavior | Python source | C++ status |
|---|----------|---------------|------------|
| 1 | **Interactive TTY warning.** If both stdin and stdout are TTYs and `--write-media` is not given, print a warning to stderr and wait for Enter before proceeding. `Ctrl-C` prints "Operation canceled." to stderr and exits cleanly. | `util.py:_run_tts()` | `planned` |
| 2 | **Audio default is stdout.** When `--write-media` is omitted, raw MP3 bytes are written to `stdout` (not a file). | `util.py:_run_tts()` | `planned` |
| 3 | **Subtitle default is no output.** When `--write-subtitles` is omitted, no SRT is written anywhere. `-` sends it to stderr. | `util.py:_run_tts()` | `planned` |
| 4 | **File input encoding.** `--file` reads the file with UTF-8 encoding. `-` and `/dev/stdin` are treated as stdin. | `util.py:amain()` | `planned` |
| 5 | **No `--format` option.** The audio format is hardcoded to `audio-24khz-48kbitrate-mono-mp3` in the WebSocket `speech.config` message. There is no CLI flag to change it. | `communicate.py`, `constants.py` | `exact` (no format flag) |
| 6 | **No custom SSML.** The Python project explicitly removed custom SSML support because the service only permits the single `<voice><prosody>` structure that the library already generates. | `README.md` | `exact` (no custom-SSML flag) |
| 7 | **Exit code 0 on success,** non-zero on error. `--list-voices` calls `sys.exit(0)` explicitly. | `util.py:amain()` | `planned` |
| 8 | **Negative-value syntax.** `--rate=-50%` works; `--rate -50%` is misinterpreted by argparse as an unknown option. The README documents this limitation explicitly. C++ CLI11 has the same behavior; users must use `=`-form for negative values. | `README.md` | `exact` (same constraint) |
| 9 | **Voice list sort order.** Voices are sorted ascending by `ShortName` before display. | `util.py:_print_voices()` | `exact` |
| 10 | **Voice list columns.** `Name`, `Gender`, `ContentCategories` (comma-joined), `VoicePersonalities` (comma-joined). Formatted as a tab-aligned table using `tabulate`. | `util.py:_print_voices()` | `exact` — `VoiceFormatter` produces tabulate "simple" format: left-aligned columns padded to max width, separated by two spaces, dash separator row |

---

## `edge-playback`

**Argument parsing implementation:** `include/edge_tts/cli/PlaybackArguments.hpp` /
`src/cli/PlaybackArgumentParser.cpp`.  `PlaybackArgumentParser::parse()` is stateless
and testable.  Dispatch: `include/edge_tts/cli/PlaybackCommandDispatcher.hpp` /
`src/cli/PlaybackCommandDispatcher.cpp`.  Entry point: `apps/edge-playback/main.cpp`.

**Implementation note (deviation from Python):** The Python reference calls
`edge-tts` as a subprocess (`_run_edge_tts()`).  The C++ implementation calls
`api::Communicate::save()` directly and `media::FfmpegAudioConverter::play_mp3()`
for playback via `ffplay`.  Observable behavior (temp file lifecycle, env vars,
exit codes) is identical.

### Option matrix

| Option | Short | Argument | Default | Python behavior | C++ behavior | Status |
|--------|-------|----------|---------|-----------------|--------------|--------|
| `--mpv` | — | (flag) | `false` on Windows, `true` elsewhere | Force mpv playback on Windows. | Accepted and stored; no behavior change (ffplay always used via `FfmpegAudioConverter`). | `partial` |
| `--text` | `-t` | `STRING` | (required\*) | Forwarded verbatim to `edge-tts` subprocess. | Passed to `Communicate`. | `exact` |
| `--file` | `-f` | `PATH` | (required\*) | Forwarded verbatim to `edge-tts` subprocess. | Read via `InputLoader`; same stdin/file semantics. | `exact` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Forwarded verbatim to `edge-tts` subprocess. | Identical. | `exact` |
| `--rate` | — | `RATE` | `+0%` | Forwarded verbatim. | Identical. | `exact` |
| `--volume` | — | `VOL` | `+0%` | Forwarded verbatim. | Identical. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Forwarded verbatim. | Identical. | `exact` |
| `--proxy` | — | `URL` | (none) | Forwarded verbatim. | Parsed into args; `PlaybackCommandDispatcher` copies it into `api::CommunicateOptions::proxy` (same path as `edge-tts`). | `partial` (parsed; PlaybackCommandDispatcher forwarding not yet wired) |
| `--write-media` | — | `PATH` | N/A | **Not accepted.** | Returns parse error. | `N/A` |
| `--write-subtitles` | — | `PATH` | N/A | **Not accepted.** | Returns parse error. | `N/A` |
| `--list-voices` | `-l` | (flag) | N/A | **Not accepted.** | Returns parse error. | `N/A` |
| `--help` | `-h` | (flag) | — | Prints minimal help. | Identical. | `exact` |

\* `--text` and `--file` are mutually exclusive; exactly one must be provided.

### Behavioral notes

| # | Behavior | Python source | C++ status |
|---|----------|---------------|------------|
| 1 | **Library call model.** C++ calls `api::Communicate::save()` directly instead of spawning an `edge-tts` subprocess. Observable behavior is equivalent. | `__main__.py:_run_edge_tts()` | `deviation` (library call, not subprocess) |
| 2 | **Temp file lifecycle.** Temp `.mp3` is created in the OS temp dir and deleted on exit unless `EDGE_PLAYBACK_KEEP_TEMP` is set. Cleanup happens even on synthesis or playback errors (RAII guard). | `__main__.py:_cleanup()` | `exact` |
| 3 | **Playback.** Uses `ffplay -nodisp -autoexit <mp3>` via `FfmpegAudioConverter`. Python uses `mpv`. | `__main__.py:_play_media()` | `deviation` (ffplay instead of mpv) |
| 4 | **Windows playback.** `win32_playback.play_mp3_win32` when `--mpv` not set. | `__main__.py:_play_media()` | `planned` |
| 5 | **Dependency check.** `FfmpegAudioConverter` returns `external_process_failed` when ffplay is not on PATH, printed to stderr with exit 1. | `__main__.py:_check_deps()` | `exact` (error at playback time) |

### `edge-playback` environment variables

| Variable | Effect | C++ status |
|----------|--------|------------|
| `EDGE_PLAYBACK_DEBUG` | Print temp file path to stdout before playback. | `exact` |
| `EDGE_PLAYBACK_KEEP_TEMP` | Keep temp files after playback exits. | `exact` |
| `EDGE_PLAYBACK_MP3_FILE` | Override the MP3 temp file path. | `exact` |
| `EDGE_PLAYBACK_SRT_FILE` | Override the SRT temp file path (not yet used). | `planned` |

---

## Intentional deviations from Python

| # | Area | Python behavior | C++ behavior | Reason |
|---|------|-----------------|--------------|--------|
| 1 | `--version` output | `edge-tts 7.2.8` | `edge-tts-cpp {semver}` | Different project; version tracks C++ release |
| 2 | Subtitle output format | `tabulate` library for voice list | Custom stream formatting | No Python dependency; must match column layout exactly |
| 3 | Async runtime | `asyncio.run()` wraps all I/O | Native C++ async or `std::thread` | Language difference; external behavior unchanged |
| 4 | Sync wrappers | `stream_sync()` / `save_sync()` via `ThreadPoolExecutor` | Not planned initially | Public C++ API will be synchronous-first |

---

## Options not present in Python that will NOT be added

The following are absent from the Python CLI and will remain absent from C++ to maintain compatibility:

- `--format` (audio format is fixed)
- `--boundary-type` at CLI level (configured via library API only)
- `--timeout` at CLI level (configured via library API only)
- Custom SSML input

---

## Exit code reference

| Condition | Python exit code | C++ planned exit code |
|-----------|------------------|-----------------------|
| Success | 0 | 0 |
| `--list-voices` completes | 0 (explicit `sys.exit(0)`) | 0 |
| `--help` or `--version` | 0 | 0 |
| Invalid argument | 2 (argparse default) | 2 (CLI11 default) |
| Runtime error (network, no audio) | 1 (unhandled exception) | 1 |
| Missing deps (`edge-playback`) | 1 (explicit `sys.exit(1)`) | 1 |
| `Ctrl-C` during TTY warning | 0 (explicit clean return) | 0 |
