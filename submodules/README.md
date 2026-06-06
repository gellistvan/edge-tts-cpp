# Submodules

Third-party dependencies live here as git submodules.  Submodules are the
**preferred** dependency source — they are checked out offline, deterministic,
and do not require network access at configure time.

When a submodule directory is empty (e.g. after downloading a source archive
without submodule contents), CMake falls back automatically to
`FetchContent` if `EDGE_TTS_FETCH_DEPS=ON` (the default).  See
`docs/DEPENDENCIES.md` for the full lookup-order policy.

## Registered submodules

| Path | URL | Purpose | Status |
|------|-----|---------|--------|
| `submodules/json` | https://github.com/nlohmann/json | JSON parsing (voice list) | ✓ registered |
| `submodules/ixwebsocket` | https://github.com/machinezone/IXWebSocket | WebSocket + HTTP client | ✓ registered |

## Initialize after clone

```bash
# All submodules at once:
git submodule update --init --recursive

# Single submodule:
git submodule update --init submodules/json
git submodule update --init submodules/ixwebsocket
```

If submodules are not initialized, CMake will use FetchContent as a fallback
(requires `EDGE_TTS_FETCH_DEPS=ON`, which is the default).

## Add a new submodule

```bash
git submodule add <url> submodules/<name>
```

Then wire it in `cmake/EdgeTtsDependencies.cmake` (or `cmake/Dependencies.cmake` for
simple cases) and document it in `docs/DEPENDENCIES.md`.

## Planned (not yet added)

```bash
git submodule add https://github.com/CLIUtils/CLI11.git submodules/CLI11
git submodule add https://github.com/google/googletest.git submodules/googletest
```
