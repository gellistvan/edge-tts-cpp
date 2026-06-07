# Contributing to edge-tts-cpp

## Prerequisites

- CMake ≥ 3.24
- A C++20-capable compiler (GCC ≥ 11, Clang ≥ 14, or MSVC ≥ 19.29)
- Python ≥ 3.10 (for hygiene and documentation tests)
- `clang-format` (optional, for formatting)
- `clang-tidy` (optional, for static analysis)

## Building

### Minimal configure (library + tests only)

```bash
cmake -S . -B build \
    -DEDGE_TTS_BUILD_APPS=OFF \
    -DEDGE_TTS_BUILD_TESTS=ON
cmake --build build
```

### Full development build

```bash
cmake -S . -B build \
    -DEDGE_TTS_BUILD_APPS=ON \
    -DEDGE_TTS_BUILD_TESTS=ON \
    -DEDGE_TTS_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### CMake build options

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_TTS_BUILD_APPS` | `ON` | Build the `edge-tts` and `edge-playback` CLI apps |
| `EDGE_TTS_BUILD_TESTS` | `ON` | Build per-module test suites |
| `EDGE_TTS_BUILD_EXAMPLES` | `OFF` | Build example programs under `examples/` |
| `EDGE_TTS_WARNINGS_AS_ERRORS` | `OFF` | Promote all compiler warnings to errors |
| `EDGE_TTS_ENABLE_NETWORK_TESTS` | `OFF` | Enable tests that call the live Edge TTS service |
| `EDGE_TTS_ENABLE_SANITIZERS` | `OFF` | Enable address and UB sanitizers |
| `EDGE_TTS_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy on all compiled sources |

## Running tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run a specific test target
./build/tests/edge_tts_core_tests
```

## Code style

All source files are formatted with clang-format using the project `.clang-format` config.

```bash
# Format all source files
find include src tests apps examples -name '*.cpp' -o -name '*.hpp' \
    | xargs clang-format -i
```

## Static analysis

Enable clang-tidy during configuration:

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_CLANG_TIDY=ON
cmake --build build
```

The `.clang-tidy` config at the project root controls which checks are enabled. Start with the existing baseline and add checks as the codebase matures.

## Module conventions

- One public header directory per module under `include/edge_tts/<module>/`.
- One implementation directory per module under `src/<module>/`.
- One test target per module under `tests/<module>/`.
- Each module is a separate CMake library target (`edge_tts::<module>`).
- Dependencies flow inward: `communication` → `serialization` → `core` → `common`. The `media` module is standalone. No circular dependencies.

## Dependency policy

Third-party libraries live as Git submodules under `submodules/` and are wired in `cmake/Dependencies.cmake`. Do not add `find_package()` calls for libraries that can be built from source as submodules. See the README for the planned dependency list.

## Network tests

Network tests require two gates to be satisfied:

1. **Compile-time gate:** build with `-DEDGE_TTS_ENABLE_NETWORK_TESTS=ON`.
2. **Run-time gate:** set the environment variable `EDGE_TTS_RUN_NETWORK_TESTS=1` when invoking CTest.  Tests skip silently when the variable is absent so the binaries pass in environments without outbound internet access.

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
cmake --build build
EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -R network --output-on-failure
```

Do not enable network tests in CI unless the environment has reliable outbound TLS access to `speech.platform.bing.com`.

See `docs/TESTING.md` for the full testing pyramid and per-target descriptions.

## Repository hygiene

The project enforces automated hygiene rules that run as part of the normal Python test group.
Run them directly with:

```bash
python3 tests/tools/test_repository_hygiene.py
```

Or via CTest (runs as `edge_tts_repository_hygiene_tests`):

```bash
ctest --test-dir build -R edge_tts_repository_hygiene_tests --output-on-failure
```

### No fake clients in production source

`FakeHttpClient`, `FakeWebSocketClient`, and `FakeProcessRunner` are test doubles.
They must **only** appear in:
- Their own definition files (`src/communication/Fake*.cpp`, `include/edge_tts/communication/Fake*.hpp`)
- Test files (`tests/**`)

Production source (`src/`, `include/edge_tts/`, `apps/`) must never `#include` any `Fake*.hpp` header.
The hygiene test (`test_repository_hygiene.py`) catches violations at CI time.

### No build artifacts in git

Build output (`CMakeFiles/`, `build/`, `*.o`, `*.a`, `*.so`, binaries) must never be committed.
The `.gitignore` at the repo root excludes all known build directories. If you accidentally stage
build artifacts, remove them with:

```bash
git rm --cached <file>
echo "<file>" >> .gitignore
```

The hygiene test checks `git ls-files` and fails if any tracked file matches build artifact patterns.

### Skeleton file quarantine

Several skeleton files from the original project scaffold have been permanently removed:
- `src/serialization/EdgeToken.cpp` / `include/edge_tts/serialization/EdgeToken.hpp`
- `src/communication/WebSocketTransport.cpp` / `include/edge_tts/communication/WebSocketTransport.hpp`
- `include/edge_tts/communication/Transport.hpp`

Do not re-create these files. The hygiene test fails if they reappear on disk.

### TODO comment format

Legitimate future-work comments are allowed. Use one of these formats:

```cpp
// TODO(#123): replace with real implementation once ixwebsocket gains proxy support
// TODO: tracked in issue #456 — remove after next ixwebsocket release
```

**Forbidden**: bare skeleton placeholders that carry no actionable context:
```cpp
return "TODO_TRUSTED_CLIENT_TOKEN";  // ← banned by hygiene test
throw NetworkError{"WebSocket transport is not implemented yet"};  // ← banned
```

The hygiene test scans `src/`, `include/edge_tts/`, and `apps/` for these strings
and fails if they appear (comments stripped, so documenting them in `docs/` is safe).
