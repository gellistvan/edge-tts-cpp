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
| `--list-voices` | `-l` | (flag) | (required\*) | Fetch voice list from service, print a tab-aligned table (Name, Gender, ContentCategories, VoicePersonalities), sorted by `ShortName`, then `sys.exit(0)`. Mutually exclusive with `--text` and `--file`. | Identical table format and sort order. | `partial` (parser done; service call not wired) |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Voice name (short or full form). Normalized and validated at construction time. | Identical validation and normalization via `TtsConfig::validate()`. | `partial` (parser done; validation on TtsConfig construction) |
| `--rate` | — | `RATE` | `+0%` | Speech rate. Must match `^[+-]\d+%$`. Negative values require `--rate=-50%` syntax (not `--rate -50%`) to avoid argparse misparse. | Identical pattern; same negative-value syntax note in help text. | `exact` |
| `--volume` | — | `VOL` | `+0%` | Speech volume. Must match `^[+-]\d+%$`. Same negative-value syntax caveat as `--rate`. | Identical. | `exact` |
| `--pitch` | — | `PITCH` | `+0Hz` | Speech pitch. Must match `^[+-]\d+Hz$`. Same negative-value syntax caveat. | Identical. | `exact` |
| `--write-media` | — | `PATH` | (none → stdout) | Write MP3 audio to `PATH`. If omitted, audio bytes go to `stdout`. `-` explicitly selects stdout. | Identical; `-` → stdout, omitted → stdout. | `exact` |
| `--write-subtitles` | — | `PATH` | (none → no subtitles) | Write SRT subtitles to `PATH`. If omitted, no subtitles are written. `-` sends subtitles to **stderr**. | Identical; `-` → stderr, omitted → no SRT output. | `exact` |
| `--proxy` | — | `URL` | (none) | HTTP/HTTPS proxy URL forwarded verbatim to the aiohttp WebSocket and voice-list clients. | Identical; forwarded to HTTP/WebSocket client. | `exact` |
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

### Option matrix

| Option | Short | Argument | Default | Python behavior | C++ planned behavior | Status |
|--------|-------|----------|---------|-----------------|----------------------|--------|
| `--mpv` | — | (flag) | `false` on Windows, `true` elsewhere | Force mpv playback on Windows. On non-Windows, mpv is always used. | Identical. | `planned` |
| `--text` | `-t` | `STRING` | (required\*) | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--file` | `-f` | `PATH` | (required\*) | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--voice` | `-v` | `VOICE` | `en-US-EmmaMultilingualNeural` | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--rate` | — | `RATE` | `+0%` | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--volume` | — | `VOL` | `+0%` | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--pitch` | — | `PITCH` | `+0Hz` | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--proxy` | — | `URL` | (none) | Forwarded verbatim to `edge-tts` subprocess. | Identical passthrough. | `planned` |
| `--write-media` | — | `PATH` | N/A | **Not accepted.** `edge-playback` does not expose `--write-media`. | N/A. | `N/A` |
| `--write-subtitles` | — | `PATH` | N/A | **Not accepted.** `edge-playback` does not expose `--write-subtitles`. | N/A. | `N/A` |
| `--list-voices` | `-l` | (flag) | N/A | **Not accepted.** `edge-playback` does not expose `--list-voices`. | N/A. | `N/A` |
| `--help` | `-h` | (flag) | — | Prints minimal help noting "See `edge-tts` for additional arguments." | Identical behavior. | `planned` |

\* Forwarded arguments are passed through `parse_known_args`; `edge-playback` only strips `--mpv`.

### Behavioral notes

| # | Behavior | Python source | C++ status |
|---|----------|---------------|------------|
| 1 | **Subprocess model.** `edge-playback` calls `edge-tts --write-media=<tmp.mp3> [--write-subtitles=<tmp.srt>] <passthrough-args>` as a subprocess. It is not a library call. | `__main__.py:_run_edge_tts()` | `planned` |
| 2 | **Temp file lifecycle.** Temp `.mp3` (and `.srt` when using mpv) are created in the OS temp directory. Both are deleted on exit unless `EDGE_PLAYBACK_KEEP_TEMP` is set. | `__main__.py:_cleanup()` | `planned` |
| 3 | **mpv command line.** `mpv --msg-level=all=error,statusline=status [--sub-file=<srt>] <mp3>` | `__main__.py:_play_media()` | `planned` (Linux/macOS); best-effort on Windows |
| 4 | **Windows playback.** `win32_playback.play_mp3_win32` is used when on Windows and `--mpv` is not set. | `__main__.py:_play_media()` | `planned` |
| 5 | **Dependency check.** Before any work, verifies `edge-tts` (and `mpv` on non-Windows) exist on `$PATH`. Prints missing deps to stderr and exits with code 1. | `__main__.py:_check_deps()` | `planned` |

### `edge-playback` environment variables

| Variable | Effect | C++ status |
|----------|--------|------------|
| `EDGE_PLAYBACK_DEBUG` | Print temp file paths to stdout before playback. | `planned` |
| `EDGE_PLAYBACK_KEEP_TEMP` | Keep temp files after playback exits. | `planned` |
| `EDGE_PLAYBACK_MP3_FILE` | Override the MP3 temp file path (skip creation). | `planned` |
| `EDGE_PLAYBACK_SRT_FILE` | Override the SRT temp file path (skip creation). | `planned` |

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
