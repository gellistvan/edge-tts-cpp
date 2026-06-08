# Dependency Rules

This document is the single source of truth for which CMake module targets may
depend on which other targets.  Violations must be caught in code review;
every `target_link_libraries` call in the root `CMakeLists.txt` must be
justified by this table.

---

## Allowed dependency matrix

A ✓ in cell (row → column) means the **row** module may depend on the **column**
module.  A blank cell means the dependency is **forbidden**.

| Depending module | common | core | serialization | subtitle | media | communication | api | cli |
|------------------|:------:|:----:|:-------------:|:--------:|:-----:|:-------------:|:---:|:---:|
| `common`         | —      |      |               |          |       |               |     |     |
| `core`           | ✓      | —    |               |          |       |               |     |     |
| `serialization`  | ✓      | ✓    | —             |          |       |               |     |     |
| `subtitle`       | ✓ (transitively via core) | ✓ | | — |  |             |     |     |
| `media`          | ✓      |      |               |          | —     |               |     |     |
| `communication`  | ✓      | ✓    | ✓             |          |       | —             |     |     |
| `api`            | ✓      | ✓    | ✓             | ✓        | ✓     | ✓             | —   |     |
| `cli`            | ✓      | ✓    |               | ✓        | ✓     |               | ✓   | —   |

**Rules in plain language:**

1. `common` has no upstream dependencies.  It must remain a pure utility/error
   layer with zero `#include`s outside the standard library.
2. `core` depends only on `common`.  It must not include any networking,
   JSON, serialization, or process-execution headers.
3. `serialization` depends on `core` and `common`.  It must not perform
   I/O or open network connections.
4. `subtitle` depends on `core` (and `common` transitively).  It must not
   include serialization or communication headers.
5. `media` depends on `common` only.  It must not include TTS domain types
   or protocol headers.
6. `communication` depends on `common`, `core`, and `serialization` only.
   It is pure transport orchestration (WebSocket/HTTP framing, protocol parsing,
   session lifecycle).  It must not include `subtitle` or `media` — those belong
   in `api`.  It must not depend on `api` or `cli`.
7. `api` is the public synthesis facade.  It depends on `communication`,
   `serialization`, `subtitle`, `media`, `core`, and `common`.  It must not
   depend on `cli`.
8. `cli` is the application-layer helper shared by the `edge-tts` and
   `edge-playback` executables.  It depends on `api`, `media` (for
   `IAudioConverter` in its public headers), `subtitle` (for `SubMaker` in its
   `.cpp` files), `core`, and `common`.  It must not reach past `api` into
   `communication` or `serialization` internals.

## Forbidden dependency examples

The following would be build violations:

```cmake
# WRONG: core must not know about serialization
target_link_libraries(edge_tts_core PUBLIC edge_tts::serialization)

# WRONG: subtitle must not depend on communication
target_link_libraries(edge_tts_subtitle PUBLIC edge_tts::communication)

# WRONG: media must not depend on core domain types
target_link_libraries(edge_tts_media PUBLIC edge_tts::core)

# WRONG: communication must not depend on subtitle or media
target_link_libraries(edge_tts_communication PUBLIC edge_tts::subtitle)
target_link_libraries(edge_tts_communication PUBLIC edge_tts::media)

# WRONG: cli must not bypass api to reach communication internals
target_link_libraries(edge_tts_cli PUBLIC edge_tts::communication)
target_link_libraries(edge_tts_cli PUBLIC edge_tts::serialization)
```

## Automated boundary check

`tools/check_module_boundaries.py` scans all `#include` directives in
`include/`, `src/`, and `apps/` and reports any that violate the matrix above.
It also catches apps that include headers via relative paths into `src/`
(private headers).

Run manually:
```bash
python3 tools/check_module_boundaries.py
```

Run via CTest (requires `EDGE_TTS_BUILD_TESTS=ON`):
```bash
ctest --test-dir build -R edge_tts_module_boundary_tests
```

The test suite in `tests/tools/test_module_boundaries.py` verifies:
- Every allowed include pattern passes
- Every forbidden include pattern is detected
- The private-header-from-app rule fires correctly
- The actual project tree is clean

## How to verify CMake links

```bash
# Configure with apps enabled to inspect the link graph
cmake -S . -B build -DEDGE_TTS_BUILD_APPS=ON
cmake --build build --target edge_tts_core       # must not pull in serialization
cmake --build build --target edge_tts_serialization  # must not pull in communication
```

Check link dependencies in the build system:
```bash
# Inspect what a target links (requires cmake >= 3.20)
cmake --build build --target edge_tts_core -- VERBOSE=1 2>&1 | grep -o "libedge.*\.a"
```

## Adding a new dependency

1. Verify it is allowed by the matrix above.
2. If it creates a cycle, the design is wrong — refactor instead.
3. Add the `target_link_libraries` call in `CMakeLists.txt`.
4. Update this table.
5. Add a sentence to `docs/MODULES.md` under the relevant module.

---

## CMake target names

| Module | CMake target | Alias |
|--------|-------------|-------|
| common | `edge_tts_common` | `edge_tts::common` |
| core | `edge_tts_core` | `edge_tts::core` |
| serialization | `edge_tts_serialization` | `edge_tts::serialization` |
| subtitle | `edge_tts_subtitle` | `edge_tts::subtitle` |
| media | `edge_tts_media` | `edge_tts::media` |
| communication | `edge_tts_communication` | `edge_tts::communication` |
| api | `edge_tts_api` | `edge_tts::api` |
| cli | `edge_tts_cli` | `edge_tts::cli` |
| *(aggregate)* | `edge_tts` | `edge_tts::edge_tts` |
| *(test-only)* | `edge_tts_test_support` | *(no alias — test use only)* |

The aggregate target `edge_tts::edge_tts` links all modules for use in
examples.  Applications and tests must link specific module targets.

`edge_tts_test_support` is defined in `tests/CMakeLists.txt` and is only
available when `EDGE_TTS_BUILD_TESTS=ON`.  It must **never** be linked from
production library targets (`edge_tts_media`, `edge_tts_communication`, etc.)
or from CLI app targets (`edge-tts`, `edge-playback`).

## Test targets

| Module | Test executable | Extra link |
|--------|----------------|------------|
| common | `edge_tts_common_tests` | — |
| core | `edge_tts_core_tests` | — |
| serialization | `edge_tts_serialization_tests` | — |
| subtitle | `edge_tts_subtitle_tests` | — |
| media | `edge_tts_media_tests` | `edge_tts_test_support` |
| communication | `edge_tts_communication_tests` | `edge_tts_test_support` |
| api | `edge_tts_api_tests` | `edge_tts_test_support` |
| cli | `edge_tts_cli_tests` | `edge_tts_test_support` |
