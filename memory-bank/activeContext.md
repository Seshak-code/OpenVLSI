# Active Context

**Last session: Claude Code (dev), 2026-10-24 — TreeSPICE parity finished at the device level; scale track S1, S3.0–S3.2**

**TreeSPICE PRs, merge in order (merge commits):** #19 `mac/parity` → main, then #20 `mac/parity3` (retarget to
main), then #21 `mac/scale`. Each branch's tip passed the full matrix.

**#20 `mac/parity3` (parity 3):**
- **Diode:** VP, RSW, self-heating (THERMAL, RTH0/CTH0), LEVEL 3.
- **Also:** LTRA; BJT IBE/IBC and quasi-saturation; E/G FREQ (as an xfer); XSPICE digital, analog and xtradev
  models; commas in A ports.
- **Deliberate divergences** (where ngspice disagrees with its own DC slope or transient), each checked in
  test_engine_fixes:
  - RSW: transient sidewall charge; bottom-characteristic AC.
  - Self-heating AC: CTH0, charge-current power, IKF.
  - LEVEL 3: M applied once; XOM in Angstrom.
- **Parity left (explicit errors):** CPL/TXL, BSIM1/2, HiSIM, BSIM3 < 3.2, exact BSIM4v5–v7, MESA. The owner
  chose the scale track over these.

**#21 `mac/scale` (S1, memory per device; `bench/scale_probe`):**
- **Changes:**
  - OSDI setup sharing; `.options bypass=0` memory mode (owner: keep compact bypass as the default, the switch
    for memory).
  - Contraction singles.
  - CSR binary search.
  - Flat OSDI caches.
  - `.save` narrows storage; `treespice -r` streams a full `.tran`.
- **Results:** BSIM4 ~13 → 3.5 KB/device at the transient peak; MOS1 1.5 KB/device. Scale ladder 1e4 stages
  28 → 13 s.
- **Next:** slot-indexed stamps, then S3: a hierarchical plan per cell type, shared factors, and
  class-compressed state.

**`mac/s3` (S3.0–S3.2, 1e9-cell arrays; design `docs/scale-s3.md`; owner: exact equivalence, metastable = warning):**
- `.options hier` keeps the instance tree and solves the quotient circuit of the coarsest equitable partition
  (`netlist/hier_partition.h`); `.op`, `.dc`, `.tran`. AC etc. refused for now (S3.3).
- **Gate met:** 1.05e9 6T cells (`bench/s3_sram.py`), write then read: 36 unknowns, 2.2 s, 498 MB. 8k cells:
  4e-17 V from flat, 0.03 s vs 19.3 s.
- **Next:** S3.3 (AC/noise/sens on the quotient, multi-member I/L/Q/J classes, path `.ic`), parser peak memory.

**Results** (full matrix on every commit): run_diff 1234 PASS / 0 FAIL; Xyce 193 PASS (two LTRA cases time out,
ngspice is as slow); cli_check 29 PASS; ctest 4/4.

**OpenVLSI `openvlsi/ci-weekly`:**
- Windows fixes: `_environ` for `GetEnvironmentStringsA` (afdf2ee); no designated initializers in
  test_eda_drivers (de3548f).
- Linux CI found a hang in the parallel batch barrier: a lost wakeup and a cancel use-after-return (fixed in
  66fb210).
- All three platforms green (run 40104573881). **OpenVLSI PR #21** (`openvlsi/ci-weekly` → main) is open for the
  owner to merge. It also moves the engine to TreeSPICE edfa9b7.

**ngspice-47 source** (the parity reference): `~/eda/ngspice-src/ngspice-47`.

---

**Last session: Claude Code (dev), 2026-10-19 (cont.) — TreeSPICE ngspice parity on `mac/parity3` (PR #19 open: mac/parity -> main)**

**TreeSPICE `mac/parity3` (pushed to `treespice`; open its PR to main after #19 merges):**
- **XSPICE digital:** complete except d_tristate inertial.
  - Flip-flops and latches: jk/t/sr flip-flops, d/sr latches.
  - open_c/e, fdiv, lut, source (vector file), osc, pwm, state (state table), ram.
- **XSPICE analog:** ota, ilimit, oneshot, multi_input_pwl, filesource, pwlts, xfer (AC/DC; transient is an error).
- **XSPICE xtradev:** zener, potentiometer, capacitoric, inductoric, sidiode, pswitch, memristor, core, lcouple.
- **XSPICE input convergence limiting:** ngspice's mifload default, max(0.25 |last|, 0.1) per Newton iteration.
- **Engine-wide fixes:**
  - Transient used `h > 1e-15` as its "is transient?" test, so femtosecond steps stamped DC sources. The test is now h > 0.
  - Corners closer than 5e-5·maxStep merge, as ngspice's CKTminBreak.
- **BJT:** IBE/IBC; Kull quasi-saturation (RCO..QCO, an 8th junction terminal, kJT).
- **Small items:** switch negative hysteresis; SFFM's full form with TSTOP defaults; diode TREF and SOA limits.
- **Deliberate divergences** (each checked analytically in test_engine_fixes, with ngspice's number in the comment):
  - inductoric AC (ngspice gives jωL).
  - lcouple AC sign.
  - pswitch AC (−1/r).
  - BJT BC IS times AREA² by default.
  - Quasi-saturation AC uses a wrong Irci partial.
- **Results** (last full matrix): run_diff 1190 PASS / 0 FAIL; Xyce 193; cli 0; ctest 4/4.
- **Parity left:**
  - **XSPICE:** delay, astate and seegen are step-dependent in ngspice (they are left unsupported on purpose). Also cmeter, lmeter, the real/int event nodes, ota/gain noise, xfer Touchstone.
  - **LTRA**, CPL/TXL.
  - **Diode:** RSW (sidewall node; ngspice drives the sidewall current from the bottom vd when NS is not given), VP (soft recovery), RTH0/CTH0, LEVEL 3.
  - **BJT:** RTH0; quasi-saturation with AREA/M.
  - **Other models:** BSIM1/2, B3SOI (bsim3soi_*), BSIM3 v3.0/3.1, BSIM4 v4.5–4.7 (separate ngspice models), MESA, HiSIM, E/G FREQ (ngspice translates it to xfer).

**OpenVLSI `openvlsi/ci-weekly`:**
- Linux and macOS are green.
- Windows got past Configure (stderr macro clash in yosys_log_parser fixed, 6be5772). Run 39297868166 is pending.
- Open the OpenVLSI PR once it's green.

---

**Last session: Claude Code (dev), 2026-10-19 — TreeSPICE ngspice parity (PR #17 open, PR #18 stacked on it); OpenVLSI CI fixes**

**Owner's order (2026-10-18):**
1. TreeSPICE macOS feature-complete: ngspice parity.
2. Scale track S1/S3: about 1–2 KB/device; isomorphic sharing so 1e9 SRAM-like arrays fit.
3. Spectre-class analyses: PSS, HB, stb, MC mismatch, Spectre syntax.
4. S2/S4: partitioning, multirate, GPU device evaluation.

CI/CD is secondary.

**TreeSPICE, `mac/parity2` (PR #18, base `mac/parity` = PR #17):**
- **Devices:**
  - BJT: PTF; the TLEV/TLEVC temperature set.
  - IC= (D/Q/J/MOS) and MOS OFF.
  - MOS6 and MOS9.
  - VDMOS through OSDI (`tests/models/vdmos_ng.va`, built by `build_osdi.sh`).
  - MESFET (Z) on the JFET path.
  - The rest of the diode card.
- **Fixes:**
  - The OSDI AC/noise Jacobian came from a bypassed cache.
  - JFET M.
  - Flicker noise M·KF·(I/M)^AF.
- **Also:**
  - XSPICE: inertial delay, ~ outputs, A devices in subckts, and 8 stateless analog code models.
  - URC.
  - `-r` for .noise/.tf/.sens.
- **Results:** run_diff 1108 PASS / 0 FAIL; Xyce 193; cli_check clean; ctest 4/4.
- **After #17 merges:** retarget #18 to main, then rebuild OSDI (`tests/build_osdi.sh`).

**Parity left (explicit errors today):**
- **XSPICE:**
  - Stateful analog code models: int, d_dt, s_xfer, slew, hyst, oneshot, sine/square/triangle, delay, and xtradev devices. XSPICE integration is BE (cm_static_integrate). They need per-instance history at the accepted step.
  - Digital models: jkff, tff, srff, dlatch, srlatch, open_c/e, osc, source, state, ram, lut.
  - d_tristate inertial.
- **LTRA:** convolution over the full time history, about 4,300 lines in ngspice. Also CPL/TXL.
- **BJT:** quasi-saturation (RCO/VO/GAMMA/QCO; needs an 8th junction terminal), RTH0, IBE/IBC.
- **Diode:** RSW, VP, LEVEL 3.
- **Legacy and other models:** MESA (Z levels 2–4), BSIM1/2, BSIM3 < 3.2, NQSMOD, HiSIM.

**OpenVLSI, `openvlsi/ci-weekly`:**
- Linux is green (QT_QPA_PLATFORM=offscreen; NOMINMAX / _USE_MATH_DEFINES on WIN32).
- Windows: Win32 Subprocess (CreateProcess + job object) and a portable rtl_flow tool search. Run 39256298282 is pending.
- Open the OpenVLSI PR once Windows is green.

---

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
