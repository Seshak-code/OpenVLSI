# System Patterns

Code structure comes from the code itself through graphify (`graphify query`,
`path`, `explain`; see CLAUDE.md). This file only adds the rules that most often bite.

## Dependency direction (strict, never violate)

```
gui/ → ai_api/ → compute/ → physics/ → autodiff/
                 ↓            ↓
           components/ ←────┘
```

Dependencies flow **downward only**. Lower layers never import from higher
layers. Verify with `python3 scripts/check_layering.py` and
`python3 tools/check_engine_isolation.py`.

## Patterns that most often bite

- AI actions go through `AiActionEnvelope`, never direct `QGraphicsScene`.
- gRPC/orchestration code guarded by `#ifdef ACUTESIM_ENABLE_ORCHESTRATION`.
- FP determinism mandatory (`-fno-fast-math`, `/fp:strict`, `FE_TONEAREST`).
- Solver tier routing and the fail-closed convergence/parser behavior:
  `acutesim_engine/ENGINE_HANDOFF.md` §2 (architecture as of now).

**Do not write code maps here — they drift. Query the graph or read the source.**
