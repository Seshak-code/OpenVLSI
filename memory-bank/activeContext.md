# Active Context

**Last session: Claude Code (dev), 2026-10-25 (cont.) — M2 slice 4: PAC and PNoise**

- **PRs, merge in order:**
  1. TreeSPICE **#28** (`mac/pss`; still open);
  2. TreeSPICE `mac/pac` (it builds on #28; its PR opens once #28 merges, no stacking);
  3. OpenVLSI `rebuild/m2-s4` (it pins the engine at `mac/pac`'s head).
- **Engine:**
  - `.pac dec n f1 f2 maxsideband` and `.pnoise v(out[,ref]) dec n f1 f2 maxsideband [relharm]`, about the deck's
    `.pss`, by the conversion matrix.
  - PSS fixes:
    - the Jacobian runs only columns that hold state (charge or flux);
    - replays keep the base run's steps;
    - a mode beyond what one period resolves is refused;
    - the cold start stamps sources at its true time.
- **OpenVLSI:**
  - ADE analyses `pac` and `pnoise`;
  - the calculator's `VPAC(net, m)` and `VPN(out)`;
  - `tools/m2_gate.py` passes 24 of 24 checks.
  - CMake: `treespice` beside each executable is now copied on every build. As a POST_BUILD step it went stale on
    engine-only changes.
- **Next:** M2 slice 5, HB, on the same conversion matrix. A brute-force phase-noise reference needs TRNOISE.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — M2 slice 3: PSS**

- **PRs, merge in order:** TreeSPICE **#28** (`mac/pss`), then OpenVLSI `rebuild/m2-s3`. The latter pins the engine at
  #28's head; afterwards, move the pointer to #28's merge commit.
- **Engine:** `.pss fund tstab oscnob points harms [sciter]`, by shooting Newton.
  - `oscnob` 0: a driven circuit. A node: an oscillator, its period an unknown.
  - Writes "Periodic Steady State" and "PSS Harmonics" raw plots.
  - `advanceTo` no longer leaves sliver steps before a stop.
- **OpenVLSI:**
  - ADE analysis `pss`;
  - the calculator's `VPSS`/`IPSS`, `VH`/`IH`, `harmonic`, `thd`, `fundamental`;
  - Plot Net on a PSS run uses `VPSS` (or `VH`);
  - `tools/m2_gate.py` passes 14 of 14 checks, with its PDK paths made absolute.
- **Lesson:** ngspice's own `.pss` is no reference. It treats every circuit as an oscillator and converges at a wrong
  frequency, so long transients are the reference.
- **Also seen:** on the GHz VCO, ngspice's transient swing is first order in its step (0.95 mV high at 1 ps).
- **Next:** M2 slice 4, PAC and PNoise about the PSS.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — M2 slice 2: stb**

- **PRs, merge in order:** TreeSPICE **#27** (`mac/stb`), then OpenVLSI `rebuild/m2-s2` (it pins the engine at #27's
  head; afterwards move the pointer to the merge commit).
- **Engine:** `.stb dec n f1 f2 Vprobe`, by Middlebrook's double injection. It writes a "Stability Analysis" raw plot
  and logs the margins.
- **OpenVLSI:**
  - ADE analysis `stb`;
  - `STB(loopgain)` in the calculator;
  - an `iprobe` cell in analogLib;
  - `tools/m2_gate.py`: 6 of 6 checks.
- **Lesson:** a loop broken at the non-inverting input is another path (through the mirror's pole), so break where
  the loop closes.
- **Next:** M2 slice 3, PSS (shooting Newton), checked against a long transient's last period and ngspice's `.pss`.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — M2 begins: ADE Assembler (M2 slice 1)**

- **M2 plan:** in `docs/rebuild.md` (owner's order: Assembler, then RF in TreeSPICE, then Spectre X).
  - Slices: stb → PSS → PAC/PNoise → HB → Spectre X.
  - Gate: an LC VCO and a mixer on SG13G2; op-amp stb; Assembler yields; a measured Spectre X speedup.
- **M1 step 5:** stands on the automated bundle check. The owner has no clean account or Developer ID, so the bundle is
  signed ad hoc.
- **`rebuild/m2-s1`:**
  - New commands: `ade_summary` (worst point and margin with its derivation; yields), `ade_compare`, `ade_run_plan`
    (a plan that fails to prepare marks its prepared runs stopped).
  - Variables per test.
  - ADE Explorer: Summary and Compare tabs, Run Plan.
- **Next:** M2 slice 2, stb in TreeSPICE (Middlebrook/Tian loop gain at a probe), checked against the M1 op-amp's
  loop broken by hand.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — the M1 gate passes whole**

- **`rebuild/bundle`:** `tools/deploy_macos.sh` closes the bundle's dependencies that `macdeployqt` misses with
  Homebrew's Qt: `@rpath` ones such as QtDBus and libbrotlicommon, and absolute Homebrew ones such as libpng. It strips
  rpaths that leave the bundle and signs ad hoc.
- **The gate's bundle step** now also runs the bundled treespice and verifies the signature.
- **Result:** `tools/m1_gate.py --bundle` passes 28 of 28 checks.
- **The owner's part:** run the bundle on a clean account; Developer ID signing and notarization.
- **This Mac:** Homebrew's Qt plugin links were repaired with `brew unlink qt && brew link qt`.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — rebuild M1 slice 8: the M1 gate runs**

- **PRs, merge in order:** TreeSPICE **#26** (`mac/mc`), then OpenVLSI **#33** (`rebuild/m1-s8`). #33 pins the engine at
  b3719cf; after #26 merges, move the pointer to its merge commit.
  - #26: seeded `agauss`/`gauss`/`unif`/`aunif` drawn per instance, and `.tran tmax` (it was dropped).
  - #33:
    - output derivations (equation, parameters, steps);
    - `pdk_install` (analogLib, sg13g2);
    - ADE temperature sweeps and Monte Carlo;
    - `tools/m1_gate.py`;
    - the `bundle` target.
- **The gate:** 24 of 24 checks pass, 46 s, through MCP only, against ngspice.
  - 30 corner and temperature points; the worst output disagreement is 0.02 of its tolerance.
  - A 200-sample Monte Carlo compared by a KS test, D ≤ 0.135 against 0.163.
- **Gate step 5 is open:** the bundle doesn't start on this Mac. Homebrew's `share/qt/plugins` lacks its `platforms`
  link; `brew link --overwrite qt` is the owner's call. The clean-account run is the owner's.
- **Next, after M1:** M2 (ADE Assembler, Spectre RF in TreeSPICE, ...) or the remaining M1 polish: buses,
  edit-in-place, inherited nets, CDF callbacks, Library Manager copy and locks.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — rebuild M1 slice 7**

- **PR #32** (`rebuild/m1-s7`): the waveform viewer.
  - `app/plot` and `app/waveform_viewer` draw the traces, with cursors and a calculator line.
  - `wave_eval` is the calculator as a command.
  - Cross-probing works both ways: `waveform_probe` (Plot Net) and `editor_select_net`.
  - ADE results open in the viewer on a double-click.
- **Next:** slice 8.
  - SG13G2 primitives: `spice` templates on the IHP models.
  - Monte Carlo: process and mismatch from the PDK.
  - The M1 gate: a two-stage Miller op-amp (op/ac/tran/noise over 5 corners plus a temperature sweep, and a
    200-sample Monte Carlo) matched against ngspice, driven once through MCP alone; the bundle on a clean account.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — rebuild M1 slice 6**

- **PR #31** (`rebuild/m1-s6`): ADE Explorer.
  - `sim/calc`: the Cadence-style calculator.
  - `sim/ade`: the setup, its points, prepare/execute, and the `Jobs` that run in the background.
  - New commands: `ade_setup_get/set`, `ade_run` (`wait: false` runs it in the background), `ade_stop`,
    `ade_history_list/get`.
  - `app/ade_explorer`: the window, reached by `editor_open` on a maestro view.
  - Each run is a history on disk: `simulation/<lib>/<cell>/<view>/Interactive.N`.
- **Changed:** simulations run `--cpu` (double precision). `Subprocess` is fork-safe for parallel runs.
- **Next:**
  - slice 7, the waveform viewer (plots, cursors, the calculator UI, cross-probing);
  - then slice 8: SG13G2 primitives, Monte Carlo, and the M1 gate (a two-stage Miller op-amp, matched against
    ngspice, driven once through MCP).

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — rebuild M1 slice 5**

- **PR #30** (`rebuild/m1-s5`): the netlister (`sim/netlist`) and simulation (`sim/run`).
  - Primitives are netlisted through a `spice` cell property (prefix + template) with `params` defaults.
  - A config view sets `top`, `viewList` and `bindings`.
  - New commands: `netlist`, `simulate` and `results_get`.
  - `simulate` runs the `treespice` copied beside each executable, and keeps each run in its own `simulation/.../runN` directory.
- **Fixed in `flow/subprocess`:** an EINTR in `waitpid` read as exit 0, so a failing run looked like a success under lldb.
- **Next slice:** ADE Explorer.
  - Its state is a `maestro`-like view: tests, analyses, variables, corners and outputs.
  - Runs happen in the background, with a results history.
  - Waveforms follow.
- **PDK:** the IHP SG13G2 primitives (with their `spice` templates) are still needed for the M1 gate.

---

**Last session: Claude Code (dev), 2026-10-25 (cont.) — rebuild M1 slice 4**

- **PR #29** (`rebuild/m1-s4`, off main after #27/#28 merged): the schematic and symbol editors.
  - `app/scene` builds the drawn items from `view_get`; `app/canvas` is a QRhiWidget (Metal) with a QPainter text
    layer; `app/view_editor` holds the tools on Virtuoso's bindkeys.
  - New commands `objects_move` and `objects_delete` (the latter replaces `object_delete`), plus the GUI-only
    `editor_open`.
  - oadb schema 3 (label justification); v2 libraries upgrade in place on open.
- **Render check:** the canvas test skips under offscreen QPA (no RHI). Run it natively to really check it:
  `QT_PLUGIN_PATH=/opt/homebrew/opt/qt/share/qt/plugins build/test_view_editor`. `OPENVLSI_GRAB=<png>` saves the
  window.
- **Next slice:** the netlister (schematic → SPICE through a config view) and simulation through TreeSPICE.
  After that: ADE Explorer, waveforms, Python scripting, the M1 gate.
- **Still to do:** buses, edit-in-place, inherited nets, CDF callbacks, stretch; Library Manager copy and locks.

---

**Last session: Claude Code (dev), 2026-10-25 — OpenVLSI rebuild M1 (docs/rebuild.md), slice 3**

- **PRs:** slice 1 #24 and the tests fix #25 are merged. Slice 2 #26 merged into `fix/missing-tests` by mistake, so
  **#27** lands it on main. **#28** (`rebuild/m1-s3`) is the Qt app shell: `OpenVLSI.app`, the CIW and the Library
  Manager. Merge #27 first. Don't stack PRs.
- **Slice 3 changes:**
  - `Command::readOnly`: windows refresh only after writes, and MCP reports `readOnlyHint`.
  - Workspace paths may contain spaces.
  - The AGL link workaround for Qt 6.9.0 on the macOS 26 SDK.
- **This machine:** `/opt/homebrew/share/qt/plugins` is stale (it has no `platforms/`). Run the app from the build
  dir with `QT_PLUGIN_PATH=/opt/homebrew/opt/qt/share/qt/plugins`. The ctest sets this itself.
- **Next slices:**
  - the schematic and symbol editors (RHI canvas);
  - the netlister and TreeSPICE simulation;
  - ADE Explorer and waveforms;
  - Python scripting with a venv per tool;
  - the M1 gate.
- **Also still to do:** Library Manager copy and locks.

---

**Last session: Claude Code (dev), 2026-10-24 — TreeSPICE parity finished at the device level; scale track S1**

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

**Codebase review (2026-10-24):** TreeSPICE PR #25 (`mac/review`) and OpenVLSI PR #22 (`review/cleanup`, pins the
engine at #25's head: merge #25 first, then move the pointer to its merge commit). Fixes: lost edits on save-on-close,
fake New Project, ORFS sign-off reading missing reports as clean, corners/temperature silently dropped, singular
solves returned as zeros, WorkerManager thread race. About 9,000 dead lines deleted across both repos. Open
(decisions): AI actions bypass `AiActionEnvelope`; `ai_api` includes `gui/`; `parseSiValue` M = mega.

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
