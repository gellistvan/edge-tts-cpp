# Submodules

Third-party dependencies should be checked out here as git submodules.

Planned examples:

```bash
git submodule add https://github.com/CLIUtils/CLI11.git submodules/CLI11
git submodule add https://github.com/nlohmann/json.git submodules/json
git submodule add https://github.com/machinezone/IXWebSocket.git submodules/ixwebsocket
git submodule add https://github.com/google/googletest.git submodules/googletest
```

Wire each dependency in `cmake/Dependencies.cmake`.
