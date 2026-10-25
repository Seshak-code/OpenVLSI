# Tech Context

The full, authoritative build/test reference is **`CLAUDE.md`** (shared by both
agents — read it). This file only records the quick-start and the traps that
are not obvious from it.

## Quick start (macOS, primary)

```bash
cmake -B build -S . -DFETCHCONTENT_UPDATES_DISCONNECTED=ON   # after deletions/additions
cmake --build build -j10
cd build && ctest
# engine checks live in the submodule: acutesim_engine/ENGINE_HANDOFF.md section 3 (run_diff: 1234 PASS, 0 FAIL)
```

## Traps

- `.gitignore` ignores `tests/`, `CLAUDE.md`, `acutesim_engine/ENGINE_HANDOFF.md`, `.claude/`,
  `datasets/`: new files there need `git add -f` (tracked files commit fine).
- The shell `grep` wrapper skips ignored test files — use `command grep`.
- After source deletions, re-run cmake configure or MOC autogen fails.
- WASM serve port is 8080; emsdk at `/Users/seshak/Qt/emsdk/`.
- Commit convention: conventional prefix, explain *why* (see git log for the
  house style).
