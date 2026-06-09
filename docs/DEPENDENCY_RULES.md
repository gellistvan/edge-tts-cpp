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

## Umbrella pseudo-module

`include/edge_tts/edge_tts.hpp` sits directly at `include/edge_tts/` rather than
inside a module subdirectory.  The boundary checker treats it as the `"umbrella"`
pseudo-module with the following allowed includes:

| Umbrella may include | Umbrella must NOT include |
|----------------------|---------------------------|
| `common/` | `cli/` |
| `core/` | `media/` |
| `api/` | `communication/` |
| | `serialization/` |
| | `Fake*.hpp` |
| | `test_support` |

The umbrella header is the **recommended entry point** for external consumers.
It must never expose internal transport, serialization, or application-layer
modules.

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

## Target ownership rules

### Rule 1 — Consumer-facing targets must not expose internal modules

`edge_tts::tts` is the stable public consumer target.  The following must never
appear in its direct or transitive link interface via its own definition:

- `edge_tts_cli` / `edge_tts::cli` — CLI argument-parsing; app-layer only
- `edge_tts_test_support` — test-only fake clients; never in production graphs
- App executables (`edge-tts`, `edge-playback`)

Enforced by `tests/tools/test_repository_hygiene.py`
(`test_edge_tts_tts_does_not_link_internal_targets`) and by
`tests/cmake/test_public_tts_target.py`.

### Rule 2 — Internal modules must not depend upward

See the dependency matrix above.  `communication` may not depend on `api`;
`cli` must not bypass `api` to reach `communication`.

### Rule 3 — Test-support targets are test-only

`edge_tts_test_support` is only available when `EDGE_TTS_BUILD_TESTS=ON`.
It must never be linked from any production library or CLI app target.

## CMake target names

| Target | Alias | Audience |
|--------|-------|----------|
| `edge_tts_tts` | `edge_tts::tts` | **External consumers** — recommended entry point |
| `edge_tts_api` | `edge_tts::api` | External consumers (advanced) |
| `edge_tts_communication` | `edge_tts::communication` | External consumers (advanced) |
| `edge_tts_serialization` | `edge_tts::serialization` | External consumers (advanced) |
| `edge_tts_subtitle` | `edge_tts::subtitle` | External consumers (advanced) |
| `edge_tts_media` | `edge_tts::media` | External consumers (advanced) |
| `edge_tts_core` | `edge_tts::core` | External consumers (advanced) |
| `edge_tts_common` | `edge_tts::common` | External consumers (advanced) |
| `edge_tts_cli` | `edge_tts::cli` | Internal / app-layer only |
| `edge_tts` | `edge_tts::edge_tts`, `edge_tts::all` | Internal examples / broad convenience |
| `edge_tts_test_support` | *(no alias)* | Test targets only |

The aggregate target `edge_tts::edge_tts` / `edge_tts::all` links all modules
including `cli` and `media`.  Internal examples may use it.  External consumers
must use `edge_tts::tts`.

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
