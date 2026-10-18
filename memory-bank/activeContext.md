# Active Context

**Last session: Claude Code (dev), 2026-10-18 — CI/CD phase (OpenVLSI `openvlsi/ci-weekly`), TreeSPICE #15/#16 merged**

**CI/CD (owner's choice: weekly, Mondays, plus manual dispatch; Vercel deploy disabled):**
- **TreeSPICE:** green on linux-x86_64, windows-x86_64 and macos-arm64. 06:00 UTC. The oracle on Windows is MSYS2's `ngspice_con.exe`.
- **OpenVLSI:** 07:00 UTC.
  - Linux and Windows get libraries from `vcpkg.json` through the toolchain. Tcl is vcpkg's 9.0 on Windows and apt's `tcl-dev` on Linux.
  - Dawn is built without GLFW. macOS uses brew.
  - Run `ctest -LE gpu`.
- **TreeSPICE #16:** B4SOI overlap charges are now counted once, via a patch in `tests/build_osdi.sh` (Gemini's diagnosis). B4SOI now supports `.tran/.ac/.noise/.pz`.
  - Rebuild OSDI after pulling.
  - Xyce LEVEL 10 cards (BSIMSOI 3.x) are classified XYCE-DIALECT.
- **Open:**
  - `.sens` device parameters and AC sensitivity: decide true derivatives vs ngspice's zeros.
  - Carry Gemini's df7bc00 findings into main.
  - GUI process-corner no-op; parasiticResistors never stamped.
- **Next:** Phase 8, the real-time loop (incremental re-flow, a GDB stub).

---

**Last session: Claude Code (dev), 2026-10-16 — Phase 7 gate met (OpenVLSI `phase7/fw-on-silicon`, TreeSPICE PR #11)**

**Phase 7 (firmware on your own silicon):**
- **Firmware sim:** `runFirmware` (`compute/orchestration/drivers/rtl_flow`) runs the hello firmware on the RV32I core's RTL and on its routed netlist with layout SDF, in Icarus.
- **Gate:** `tests/test_firmware_on_silicon`. Correct at 7 ns, wrong at 2.5 ns, fastest correct 5.03 ns, against STA's 5.36 ns floor.
- **Lockstep:** TreeSPICE PR #11 adds `TransientHandle` (`setSource`, `advanceTo`, `voltage`). `soc::cosimLockstep` runs a UART loopback through the SPICE line (in `test_soc_hello`).
- **GUI:** Simulation → Run Firmware on Routed Core.
- **Agents:** Sonnet/Haiku did the investigations, the engine API and the loopback; Opus reviewed.
- **Next:** Phase 8, the real-time loop (incremental re-flow, a GDB stub).

---

**Earlier: Claude Code (dev), 2026-10-16 — Phase 6 gate met (OpenVLSI `phase6/rtl2gds`), Phase 4 QA round 3 fixed (TreeSPICE PR #10)**

**Phase 6 (RTL → GDS on IHP SG13G2):**
- **Flow:** `compute/orchestration/drivers/rtl_flow.{h,cpp}` runs OpenROAD-flow-scripts, then IHP's KLayout DRC and LVS decks. It sits behind WorkerManager::submitRtlToGds and the RTL pipeline panel.
- **Tools:** in `~/eda`, set up by `scripts/setup_eda_tools.sh`. Their build tools break on paths with spaces.
- **Check:** `tests/test_rtl_flow`.
  - gcd: 108 s, all checks pass.
  - RV32I at 7 ns: 963 s, timing met, DRC clean (maximal deck), LVS match.
- **Removed:** the FreePDK45 TCL generators and `ToolDriver`, about 1,750 lines.
- **Pre-existing, not chased:** `test_eda_drivers` aborts in klayout_lvs_match on `main` too.
- **Next:** Phase 7, firmware on the post-layout netlist: Verilator, SDF from OpenSTA, lockstep co-simulation.

**Phase 4 QA round 3 (Gemini):**
- BUG-5 VERIFIED.
- BUG-6 fixed again in TreeSPICE PR #10: PIVTOL on the final DC factorization.
- BUG-7 matches ngspice (its transient OP is unsettled too). Its real defect, the KCL audit missing the transient OP's gmin, is fixed.
- Next: Gemini retests PR #10. Then a Sonnet 5.5 session takes the Phase 5 entry.

---

**Earlier: Claude Code (dev), 2026-10-16 — Phase 4 done (TreeSPICE PR #9), Phase 5 gate met (OpenVLSI `phase5/soc`)**

**QA hand-off (owner, 2026-10-15/16):**
- **Phase 4 task split (owner request 2026-10-16):**
  - **Claude Code**: Benchmarks, solver scale ladder (`scale_ladder.py`), KLU ordering validation (`klu_bench`), numerical stability of delayed-pivot cap on stiff/near-singular circuits, and thread scaling/determinism.
  - **Cline / Gemini**: Adversarial SPICE testing (device bypass sensitivity on high-gain blocks, PWL source matrix, dense corner stepping, Sky130 PDK bin edges).
- **A Sonnet 5.5 session** takes the Phase 5 entry once Phase 4 is closed.
- QA does not edit engine or GUI source.

PRs:
- **TreeSPICE #8 and OpenVLSI #12:** merged.
- **OpenVLSI #13:** docs, the QA hand-off.
- **TreeSPICE #9 (`phase4/gpu`, ready for review):** Phase 4.
  - The ordering closes the KLU gap: within 1.1–3× on the small matrices, faster on the large ones.
  - Parallel device evaluation and bypass: a BSIM4 chain transient runs 3.8× faster.
  - 1e5-stage circuits now run (they died at 133 GB).
  - PWL sources.
- **OpenVLSI `phase5/soc`:** the reference SoC.
  - An RV32I model, an SRAM characterised by TreeSPICE, and the UART line co-simulated in SPICE.
  - Simulation → Run Reference SoC (Hello World).
  - The engine submodule is bumped to `6b40c10`. Merge #9 first.

Open after Phases 4 and 5:
- Device evaluation on the GPU (OSDI models are host code).
- Isomorphism reuse beyond bypass, and multirate integration.
- Lockstep, bidirectional co-simulation.
- The Yosys/OpenROAD design-flow UI (needs the tools installed).

**This file is the live cross-agent state. Claude Code auto-imports it at every
session start (`@memory-bank/activeContext.md` in CLAUDE.md). Update it at
every session end and stamp your agent.**

## Where things stand

- **Branch:** `main` — **PR #1 merged 2026-10-12 by Cline** (merge commit `43003d5`;
  `engine/spice-correctness` deleted after merge, local + remote). New work starts
  on `main` or a fresh branch off it. Stale leftover: `engine/tensor-mna`
  (8 months old, superseded). Tree clean.
- Done today: legacy TN/MPO/Schur/sparse-LU stack deleted (~40k lines); Phase 2
  analysis parity (real AC/noise, `.dc` sweeps, transient ICs/uic, PULSE/SIN
  corner stepping); fail-closed parser; GUI Verilog-A transport round-trips
  PULSE/SIN/AC; IBM power-grid datasets in `datasets/` + harnesses
  `acutesim_engine/tests/ibmpg_diff.py`, `acutesim_engine/tests/xyce_diff.py`.

## Next, in order (from `acutesim_engine/ENGINE_HANDOFF.md` §0)

1. **Golden datasets into the harness** — wire `acutesim_engine/tests/ibmpg_diff.py` and
   `acutesim_engine/tests/xyce_diff.py` into regular verification; IBM power-grid for solver
   scale, Xyce regression subset for devices.
2. **Roadmap Phase 3.**

(The two queued GUI issues were already fixed — Verilog-A transport in `c7faeb0`,
the two GUI tests in `b1d9fd2`; both verified passing 2026-10-12, offscreen Qt env.)

## Work around (known, do not chase)

- ctest pre-existing failures: `test_gpu_parity`, `test_kernel_dispatch`,
  `test_numerical_intelligence`, `test_grpc_orchestrator` (identical at
  baseline; `test_kernel_dispatch` = stale diode-clamp expectation).
- `mcSeed` never applied (every Monte Carlo run identical); temperature: Vt
  fixed at 25.85 mV vs `ambient_temp_K` default 300.15 K; MOSFET level-1 gaps
  (no Vds<0 swap, no body effect); `.include/.lib` are explicit "not supported yet"
  errors (only output cards `.options/.print/.plot/.save/.probe/.meas/.four` are ignored).
  Full backlog: `acutesim_engine/ENGINE_HANDOFF.md` §6.

## Agent coordination

Both Claude Code and Cline work this repo. Session start: sweep
(`.clinerules/00-authoritative-source.md`). Session end: update this file +
`acutesim_engine/ENGINE_HANDOFF.md` §0, stamp, commit, push (`.clinerules/01-collab-handoff.md`).
Never assume another agent's "isolated" background task was actually isolated —
check `git status` first.
