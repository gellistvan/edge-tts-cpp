# Submodules

Third-party dependencies live here as git submodules.  Submodules are the
**preferred** dependency source — they are checked out offline, deterministic,
and do not require network access at configure time.

When a submodule directory is empty (e.g. after downloading a GitHub automatic
"Source code" archive), CMake fails at configure time with a clear, actionable
error message — not a confusing compile error.  To build from such an archive:

- **Official release tarballs** include populated submodule contents and
  configure without any extra steps.
- Set `EDGE_TTS_FETCH_DEPS=ON` to let CMake download the pinned versions
  automatically (requires internet).
- Install system packages (`sudo apt install nlohmann-json3-dev`) and use
  `EDGE_TTS_FETCH_DEPS=OFF`.

See `docs/DEPENDENCIES.md` for the full lookup-order policy and
`docs/RELEASE.md` for the source archive creation process.

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
(requires `EDGE_TTS_FETCH_DEPS=ON`; this is **off by default** — set it explicitly for online builds).

## Add a new submodule

```bash
git submodule add <url> submodules/<name>
```

Then wire it in `cmake/EdgeTtsDependencies.cmake` (or `cmake/Dependencies.cmake` for
simple cases) and document it in `docs/DEPENDENCIES.md`.

