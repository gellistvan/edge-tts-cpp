# Testing Guide

## Test framework

All C++ tests use `tests/vendor/minigtest/minigtest.hpp`, a self-contained
GTest-compatible single-header runner.  No external test framework installation
is required.  The supported macro surface is:

```
TEST(Suite, Name)
EXPECT_EQ / EXPECT_NE / EXPECT_TRUE / EXPECT_FALSE
ASSERT_EQ / ASSERT_NE / ASSERT_TRUE / ASSERT_FALSE
EXPECT_THROW(stmt, ExceptionType)
EXPECT_NO_THROW(stmt)
```

## Running tests

```bash
cmake -S . -B build -DEDGE_TTS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## C++ test targets

| CTest target | Source folder | Linked module |
|---|---|---|
| `edge_tts_common_tests` | `tests/common` | `edge_tts::common` |
| `edge_tts_core_tests` | `tests/core` | `edge_tts::core` |
| `edge_tts_serialization_tests` | `tests/serialization` | `edge_tts::serialization` |
| `edge_tts_communication_tests` | `tests/communication` | `edge_tts::communication` |
| `edge_tts_media_tests` | `tests/media` | `edge_tts::media` |
| `edge_tts_subtitles_tests` | `tests/subtitles` | `edge_tts::subtitles` |

## Docs smoke tests

| CTest target | Script | What it checks |
|---|---|---|
| `edge_tts_docs_tests` | `tests/docs/test_required_reference_docs.py` | `docs/REFERENCE_BEHAVIOR.md` exists, all required headings present, ≥5 reference file paths mentioned |

The docs smoke test runs automatically with `ctest` when `EDGE_TTS_BUILD_TESTS=ON` and Python 3 is available.

## Network tests

Tests that call the live Microsoft Edge TTS service are gated by:

```bash
cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
```

Do not enable in CI unless the environment has reliable outbound internet access.

## Compatibility testing

Before implementing any networking, protocol, or text-processing feature, consult
`docs/REFERENCE_BEHAVIOR.md`.  That document describes the exact observed behavior
of the Python `edge-tts` v7.2.8 reference implementation.  Tests for the
communication and serialization layers must be written against the behaviors
documented there, not against assumptions.

## Writing tests

- Use behavior-level tests: test observable outputs, not internal implementation
  details.
- Every module should have at least one test file under `tests/<module>/`.
- New test files must include `"vendor/minigtest/minigtest.hpp"` and use the
  `TEST(Suite, Name)` macro without any `EDGE_TTS_NO_GTEST` guard.
- The `tests/common/test_main.cpp` provides the `main()` function via minigtest
  and must be included in every test target.
