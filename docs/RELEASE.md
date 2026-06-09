# Source Archive and Release Policy

This document describes how source archives for `edge-tts-cpp` are created, what
they contain, and how to build from one.

---

## What a standard git archive contains (and does not contain)

`git archive` and GitHub's automatic "Source code" downloads capture only files
tracked by the **superproject**.  Git submodules are **not** included — their
directories appear as empty folders in the resulting tarball.

That means a plain `git archive HEAD edge-tts-cpp.tar.gz` archive:

- **Contains** all project source files, CMake files, tests, docs, and tools.
- **Does not contain** `submodules/json/` or `submodules/ixwebsocket/` contents.

A user who unpacks such an archive and runs `cmake` with the defaults
(`EDGE_TTS_FETCH_DEPS=OFF`) will get a clear configure-time error for each
missing dependency, not a confusing compile error.

---

## Creating a proper release archive

Use `tools/make_release_archive.sh` to produce a tarball that **includes
populated submodule contents**:

```bash
# Make sure submodules are initialised first
git submodule update --init --recursive

# Create the archive (version from the most recent git tag)
./tools/make_release_archive.sh

# Or specify a version explicitly
./tools/make_release_archive.sh 1.0.0
```

Output: `edge-tts-cpp-<VERSION>.tar.gz` in the current directory.

The script uses `rsync` to copy each populated submodule's contents (without
`.git` metadata), so recipients can build offline without FetchContent.

### Verifying the archive

```bash
# Extract and configure — should succeed without any network access
tar xzf edge-tts-cpp-<VERSION>.tar.gz
cd edge-tts-cpp-<VERSION>
cmake --preset archive-verify
cmake --build build-archive-verify
ctest --preset offline-no-networking
```

Expected result: CMake configure succeeds (submodule contents are present),
tests pass, and no network requests are made.

---

## Building from a standard git archive (no populated submodules)

If you received an archive without submodule contents (e.g. a GitHub automatic
tarball), you have three options:

### Option 1 — Install system packages (offline-friendly)

```bash
# Ubuntu / Debian
sudo apt install nlohmann-json3-dev

# Also install ixwebsocket if you want the CLI apps
# (see docs/DEPENDENCIES.md for platform instructions)

cmake --preset offline-system
cmake --build build
```

### Option 2 — Allow FetchContent (requires internet)

```bash
cmake --preset developer
cmake --build build
```

This downloads `nlohmann/json` (tag `v3.11.3`) and `ixwebsocket` (tag
`v11.4.5`) at configure time.

### Option 3 — Libraries and tests only, no networking (minimal dependencies)

Only `nlohmann/json` is required.  `ixwebsocket` is not needed.

```bash
# System package
sudo apt install nlohmann-json3-dev

cmake --preset offline-no-networking
cmake --build build
```

---

## What happens when a dependency is missing

The CMake configure step fails **immediately** with a descriptive error message.
No ambiguous compile errors are produced.

| Scenario | Configure result |
|----------|-----------------|
| `nlohmann/json` missing, `EDGE_TTS_FETCH_DEPS=OFF` | `FATAL_ERROR` naming json and listing fixes |
| `ixwebsocket` missing, `EDGE_TTS_REQUIRE_NETWORKING=ON` | `FATAL_ERROR` naming ixwebsocket and listing fixes |
| `ixwebsocket` missing, `EDGE_TTS_REQUIRE_NETWORKING=OFF` | Warning only; stub code compiles |

---

## CMake presets reference

| Preset | `FETCH_DEPS` | `BUILD_APPS` | `REQUIRE_NETWORKING` | Use case |
|--------|-------------|-------------|---------------------|----------|
| `developer` | ON | ON | ON | Online developer build; auto-downloads missing deps |
| `offline-system` | OFF | ON | ON | Offline; system-installed packages required |
| `offline-no-networking` | OFF | OFF | OFF | Offline; only `nlohmann/json` needed |
| `archive-verify` | OFF | OFF | OFF | Verify a release archive configures correctly |

---

## Dependency versions pinned in FetchContent

| Dependency | Tag |
|------------|-----|
| nlohmann/json | `v3.11.3` |
| ixwebsocket | `v11.4.5` |

These are the same versions the submodules track.  Bump both together when
updating the submodule.

---

## Version policy

The project uses [Semantic Versioning](https://semver.org):

| Phase | Policy |
|-------|--------|
| `0.x.y` (pre-1.0) | No API stability guarantee — minor bumps may break the public API |
| `1.0.0+` | Breaking changes only on major bumps |

The authoritative version lives in the `VERSION` field of `project()` in the
root `CMakeLists.txt`.  Do not duplicate it elsewhere — every downstream artifact
(`edge_tts_cpp-config-version.cmake`, `include/edge_tts/version.hpp`, etc.) is
generated from this single source of truth at configure time.

### Bumping the version

1. Edit `CMakeLists.txt`: update the `VERSION` field in `project()`.
2. Rebuild — `include/edge_tts/version.hpp` regenerates automatically via `configure_file`.
3. Update the version badge in `README.md` (the `**Version: X.Y.Z**` line).
4. Update `docs/CONSUMING.md` (the "Current version" line in the versioning section).
5. Commit: `git commit -m "Bump version to X.Y.Z"`.

### Checklist for a release

1. Bump the version (see above).
2. Verify `ctest --output-on-failure` passes — including `edge_tts_package_version_tests`.
3. Tag the release: `git tag -a v<VERSION> -m "Release v<VERSION>"`
4. Initialise submodules if not already: `git submodule update --init --recursive`
5. Run the archive script: `./tools/make_release_archive.sh <VERSION>`
6. Smoke-test the archive (see "Verifying the archive" above).
7. Upload `edge-tts-cpp-<VERSION>.tar.gz` alongside the GitHub release.
8. Note in the release description that the GitHub automatic "Source code" archives
   do **not** include submodule contents and require either system packages or
   `EDGE_TTS_FETCH_DEPS=ON` to build.
