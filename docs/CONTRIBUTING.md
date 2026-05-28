# Contributing to edge-tts-cpp

## Prerequisites

- CMake ≥ 3.24
- A C++20-capable compiler (GCC ≥ 11, Clang ≥ 14, or MSVC ≥ 19.29)
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

Tests that hit the live Microsoft Edge TTS endpoint are gated behind `EDGE_TTS_ENABLE_NETWORK_TESTS`. Do not enable them in CI unless the environment has reliable outbound internet access.
