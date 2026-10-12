---
name: cuteapp-grep-gitignore
description: "In CuteApp the shell's grep wrapper honours .gitignore and silently skips tests/test_*.cpp; use `command grep` for exhaustive searches"
metadata:
  node_type: memory
  type: reference
  originSessionId: 75afa6b2-ece3-40ce-8ab3-66acc58c34f8
  modified: 2026-10-12T20:14:11.004Z
---

In this environment `grep` is a shell function that respects `.gitignore`. CuteApp's `.gitignore` matches `tests/test_*.cpp` even though those files are tracked, so `grep -r` misses every test file without warning. Before deleting code or claiming "no users", run `command grep -r --exclude-dir=build --exclude-dir=_deps --exclude-dir=graphify-out ...`. Without the excludes it is very slow. Related: [[acutesim-engine-direction]].
