# Tools

Developer tools for the edge-tts-cpp project.

| Tool | Purpose |
|------|---------|
| `check_module_boundaries.py` | Enforces the strict module dependency order defined in `docs/DEPENDENCY_RULES.md`. Run directly or via `ctest -R edge_tts_module_boundary_tests`. |
| `make_release_archive.sh` | Creates a source archive with populated submodule contents so the archive can be built offline without network access. |
