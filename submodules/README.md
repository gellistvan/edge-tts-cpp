# Submodules

Third-party dependencies live here as git submodules.

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
