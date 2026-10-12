# ngspice differential tests

Runs every `netlists/*.cir` through Karam (`acutesim_engine_dump`, cpu and gpu modes) and through
ngspice, then compares all top-level node voltages. Statuses: PASS, FAIL, SKIPPED (`.ac`: the engine has
no AC path), ENGINE-UNSUPPORTED (engine rejects the card, e.g. F/H/T/PWL/EXP/AREA). Exit code is 1 on any FAIL.

Prerequisites: `brew install ngspice`; `cmake --build build --target acutesim_engine_dump`; python3 (stdlib only).

    python3 tests/ngspice_diff/run_diff.py [-k name-substring] [--modes cpu,gpu] [-v]

The analysis comes from the netlist's own `.op` / `.dc` / `.tran` card. `.dc` is emulated for the engine
(one DC run per sweep point with the source value substituted). For ngspice the analysis/print cards are
dropped and a `.control` block runs the analysis and `wrdata`s the nodes. Options set on the ngspice side:
`temp=tnom=26.83` (engine hardcodes Vt = 25.85 mV) and, for `.tran`, `tmax = tstep/10` so the reference is
dense (engine samples are compared against it by linear interpolation; PULSE edges still follow `tstep`).

Tolerances (see comment in `run_diff.py`; flags `--dc-atol --dc-rtol --tran-atol --tran-rtol --temp`):
DC |err| <= 200 uV + 0.1 % of the reference (1 mOhm Norton sources/shorts give ~I*1 mOhm; 0.1 % is ngspice RELTOL).
Transient |err| <= max(5 mV, 2 % of the node's swing in the reference waveform).

Adding a circuit: drop a `.cir` with one analysis card in `netlists/`. Hierarchical cases use `.subckt`; only
top-level nodes are compared. Netlists whose floating nodes are set only by gmin (e.g. `aac_10_bridge`) are
ill-posed for comparison; `x_bridge_loaded` is the well-posed variant.
