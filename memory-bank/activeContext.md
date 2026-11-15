# Active Context

**Last session: Claude Code (dev), 2026-11-15 — M7 slices 4 and 5 and the RISC-V chip, on `rebuild/pad-ring` (after #61, merged)**

- **Done:**
  - `layout_pad_ring` (sg13g2_io corners, pads, fillers, bond pads, a pin per bond pad), `layout_seal_ring` (IHP's seal ring for any die,
    XOR-identical at 1050 um but its Metal1 filler cut to 5 um), `layout_fill` (IHP's filler scripts inside the seal ring; one fill cell
    LVS leaves out). Create > Pad Ring…, Seal Ring…, Fill….
  - Top-level LVS with IO cells and SRAM macros as black boxes: their LEF pins as abstracts (named as their CDL), joined by name, no
    substrate pin, blanked before align, pins kept through simplification; dead taps dropped; blocks with antenna cells flat in the chip.
  - `verify_drc {recommended}`; ERC keeps black boxes' pins; via arrays past 3 x 3 use V*.b1.
  - `netlist_verilog` (a schematic hierarchy as structural Verilog; cell property `verilog` set by pdk_install_cells and Make Block).
  - Flow: settings may hold paths; ADDITIONAL_CDL; a run's macros black-boxed in its LVS; DIE_AREA/CORE_AREA replace the utilization.
    Make Block places SRAM and IO cells from their libraries and makes no SPICE netlist when a macro has none.
  - `tools/rv32i` + `m7_gate` step 5: an RV32I core with firmware ROM and IHP's 64x64 SRAM through the flow, a block in 16 pads by hand,
    DRC clean but Seal.m (IHP's own seal ring gets it too), LVS matched, ERC 0, "Hello, world!" at the pads.
- **Learned:** IHP's pdn.tcl does not connect SRAM macros (VDD!/VDDARRAY!/VSS!): the block needs its own grid script. IHP's deck reads
  Activ/GatPoly fill as transistors, puts antenna diodes at the chip's top, and reports Seal.m on its own seal ring.
- **Owner's decision pending:** download IHP-Open-PDK's `RM_IHPSG13_1P_core_behavioral_bm_bist.v` (3,951 bytes) to `~/eda/ihp-verilog`
  so the RISC-V test uses IHP's SRAM model rather than ours.
- **Next:** the RISC-V chip built on screen by mouse (ui test); then Tcl and Python consoles in the app.

---

**Last session: Claude Code (dev), 2026-11-15 — building by hand with the mouse, on `rebuild/gui-build` (after #60, merged)**

- **Done:**
  - `tools/ui_build_test.py`: a library, Attach Process…, an AND2 schematic (I dialog, wires, labels, pins), its symbol, its layout
    (Instance… across libraries, a via, a Metal2 jog, pins), DRC and LVS from the Verify stage, then a top of two AND2 blocks: by mouse
    and keyboard alone. 22/22 with real macOS events on screen (watched by the owner) and 22/22 offscreen.
  - Layout editor: Instance… is a dialog (library and cell boxes); placement abuts a neighbour within 8 px; drawn edges snap to a
    same-layer edge within 6 px. Library menu: Attach Process…; `layout_tech_set`'s `tech` is an enum.
  - `ui_locate`: `tree_item` exact match first and `a > b` paths; options scrolled to the centre; clicks after a dialog closes find the
    visible window. Forms' fields carry their parameter names.
  - **Rendering fix:** on the real screen the QRhiWidget canvases showed only their text (the GPU texture was not composited; `grab()`
    saw it, so pixel checks passed). `QT_WIDGETS_RHI=1` before QApplication: the owner confirmed geometry now shows.
- **Note:** `ui_test.py --backend os` failed after the screen locked mid-session (10:10): a locked screen takes no events.
  Rerun it unlocked.
- **Next:** the GUI test grows into an RV32I SoC by hand (RTL → Implement → Make Block → top with SRAM, IO, analog → DRC/LVS →
  firmware test); then Tcl and Python consoles in the app; M7 slices 4 and 5.

---

**Last session: Claude Code (dev), 2026-11-15 — M7 slice 3, a flow run as a hard macro, on `rebuild/hard-macro`**

- **Done:**
  - `flow_make_macro {run, library?, cell?}` (Implement tab: Make Block): layout from 6_final.gds with standard cells placed from
    `sg13g2_stdcell`, own via/fill cells renamed `<cell>__<name>`; symbol from 6_final.v ports; `cdl` (6_lvs.cdl, top renamed); `spice` on
    `<cell>.sp` (top only) plus the cell library's models.
  - `verify_lvs` of a cell with no schematic and a `cdl`: against that netlist (record `schematic: cdl`).
  - `schematicCdl`: each included .SUBCKT once (two copies must agree, compared word by word); `.SUBCKT` lines stripped of `[ ] $ \ /`
    as IHP's reader strips element lines (else bus ports are dropped and a block below the top cannot pair its pins).
  - The netlister's spice templates take bus pins (`[|req_msg[3]]`).
- **Learned:** IHP's deck joins grounds through the substrate but not supplies (NWell): a cell not wired to VDD is on its own supply.
- **Gate:** M7 23/23: gcd → block (1,402 standard cells, 56 pins), LVS alone (248 devices), a top in library `chip` with the block and
  a standard cell LVS matched across three libraries, the block at transistor level out of reset (req_rdy rises 0.44 ns after the first
  edge, 74 s). M3 76/76, ctest 25/25. Make Block pressed in the real window.
- **Next:** M7 slice 4, top-level assembly: a pad ring, supplies, whole-chip DRC and LVS.

---

**Last session: Claude Code (dev), 2026-11-15 — M7 slice 2, the PDK's cells as libraries, on `rebuild/pdk-cells`**

- **Done:**
  - `pdk_install_cells {set}`: `sg13g2_stdcell`, `sg13g2_io`, `sg13g2_sram` from ORFS's ihp-sg13g2 platform (cdl/, gds/). Per cell: layout
    (GDS import; a set's files that share via cells skip them, `importGds(..., keepExisting)`), symbol from CDL ports and `*.PININFO`,
    `cdl` property (its .SUBCKT and those it places), `spice` (template on its ports, models: `<set>.sp` the cells' netlist with M as X
    through the device library, ties through ptap1/ntap1, antenna and ESD diodes dropped; plus the MOS and resistor models).
  - Extraction takes a PDK cell's port order from its CDL; the netlister loads a placed cell's models when it resolves to its own netlist.
  - File > Install PDK Cells…
- **Gate:** `tools/m7_gate.py` (new) 14/14: 84/84 standard cells simulate, 33 of 45 IO, ten SRAM macros; an AND2 by hand from
  `sg13g2_nand2_1` and `sg13g2_inv_1`, abutted, one Metal2 jog: DRC clean, LVS matched (not without the jog), extracted, truth table
  0 0 0 1 from schematic and extraction (b to y 59.1 / 60.8 ps).
- **Next:** M7 slice 3, a flow run as a hard macro.

---

**Last session: Claude Code (dev), 2026-11-15 — M7 (a SoC by hand) planned; slice 1, hierarchy through verification, on `rebuild/hier-lvs`**

- **Owner (2026-11-15):** the suite must work for building a SoC by hand from cells and libraries, through testing. Plan: `docs/rebuild.md`,
  Milestone 7 (hierarchy; the PDK's stdcells, IO and SRAM as libraries; a flow run as a hard macro; top-level assembly; testing).
- **Done (slice 1):**
  - `exportGds` follows masters into other libraries (one GDS namespace: two libraries' cells of one name are refused).
  - `schematicCdl` is hierarchical: a `.SUBCKT` per schematic cell, children first; a cell with a `cdl` cell property is included as
    that netlist (for the PDK's cells, slice 2). A global net below the top is refused (give the cell a pin).
  - `verify_extract` on a hierarchy: a cell's own wiring only (its subcells' metal subtracted), a line per subcell on the schematic's
    nets, `places` recorded; the netlister and `config_get` resolve those subcells through the config.
- **Gate:** M3 76/76: a buffer of two inverters by hand, joined on Metal3: DRC clean, LVS matched level by level (the joint deleted: BUF
  nomatch, INV match), extracted (m 1.07 fF), simulated 35.86 ps schematic, 43.83 ps extracted; a top in another library exports.
- **Next:** M7 slice 2, the PDK's standard cells, IO cells and SRAM macros as libraries.

---

**Last session: Claude Code (dev), 2026-11-15 — LVS cross-probe of devices, on `rebuild/xprobe-devices` (after #56, merged)**

- **Done:** LVS's device markers carry `schematic_instance` (a schematic-side ERC finding names its first device too). New app command
  `editor_select_instance` (any case, as LVS names devices). The markers panel's Show in Schematic selects a device's instance, or a net.
- **Checked:** `test_waveforms` (the command offscreen), M3 65/65 (the broken inverter's PMOS marker names MP), and the real window with macOS
  events: lvs.3 opened, devices that differ, right-click $1 / MP, Show in Schematic: MP selected.
- **Next:** serpentine resistors; FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; cross-probe of a flow run's LVS to its Verilog.

---

**Last session: Claude Code (dev), 2026-11-15 — corners at transistor level, on `rebuild/spice-corners` (after #55, merged)**

- **Done:**
  - `flow::SpiceCorner` / `Platform::spiceCorners`: ihp-sg13g2 slow (mos_ss, 1.08 V, 125 C), typ (mos_tt, 1.2 V, 25 C), fast (mos_ff, 1.32 V,
    -40 C), as its Liberty corners. `test_spice {corner}` swaps the MOS section, the supply and adds `.temp`; the record keeps them.
  - Test tab: a corner box beside Run at transistor level. `wave_eval`/`results_get` also read a test run's directory under testing/.
- **gcd, 30 ns:** passes at all three. Clock to req_rdy (first edge after reset): slow 779 ps, typ 502 ps, fast 338 ps; ratios 1.55 and 0.67
  against STA's 1.58 and 0.66. (The first output edges before reset differ by power-up state: not a delay.)
- **Gates:** M5 59/59 with `--pdk --osdi` (three corner runs, about 4 minutes).
- **Next:** serpentine resistors; FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices and flow runs.

---

**Last session: Claude Code (dev), 2026-11-15 — MIM capacitors, on `rebuild/mim` (after #54, merged)**

- **Done:**
  - `layout::capCell` / `layout_pcell_cap {w, l}`: the MIM w x l, TopMetal1 over it (PLUS), Metal5 0.6 um around it (MINUS), a Vmim array
    between (Vmim, not TopVia1: over a MIM the deck does not count TopVia1 as joining the plates, and neither does our connectivity).
    1.64 um to 5625 um2. The process table gained TopMetal1 and TopVia1 (a sixth metal), the palette MIM and Vmim.
  - `layout_generate`, LVS's CDL reference (`C... PLUS MINUS cap_cmim w= l= m=1`), `verify_extract` (mim_top/mim_btm to PLUS/MINUS), ERC
    (MIM plates are plates). Create > Capacitor… (Shift+C).
- **Gate:** M3 64/64: four sizes DRC-clean; a 10 x 8 um capacitor pinned on its plates, DRC clean, LVS matched (not at 12 um), extracted,
  and simulated: 121.5 fF (1.52 fF/um2), extracted 1.9 % slower from its pins' wiring.
- **Next:** serpentine resistors; FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices and flow runs;
  corners at transistor level.

---

**Last session: Claude Code (dev), 2026-11-14 — poly resistors, on `rebuild/pcells` (after #53, merged)**

- **Done:**
  - `layout::resCell` / `layout_pcell_res {type: rsil|rppd|rhigh, w, l}`: one straight strip, PLUS/MINUS on Metal1 at its ends, the PolyRes
    marker exactly the body so IHP's deck measures w and l as drawn. 15 sizes DRC-clean. Layer palette gained nSD, SalBlock, RES, PolyRes, EXTBlock.
  - `layout_generate` places resistors; LVS's CDL reference writes them (`R... PLUS MINUS rsil w= l= m= b=0 ps=0`); `verify_extract` writes
    them back with the schematic's body net. ERC treats a terminal it does not know as conducting.
  - `pdk_install` adds `r3_cmc.osdi` (beside the transistors' OSDI) to the resistors' models: without it they did not simulate.
  - Layout editor: Create > Resistor… (Shift+R).
- **Gate:** M3 56/56: a three-resistor chain generated, joined, DRC clean, LVS matched (and not when the schematic asks 3 um for a 2 um strip),
  extracted, and simulated: 161.128 uA either way.
- **Machine note:** this Mac idles to sleep with the screen locked; gate steps then took 8 to 20 minutes or a deck died with exit -1. Keep it
  awake for long runs (the app's keep-awake). With the screen locked, `ui_test.py --backend os` cannot click; the qt backend can.
- **Next:** MIM capacitors and serpentine resistors; FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices and
  flow runs; corners at transistor level.

---

**Last session: Claude Code (dev), 2026-11-14 — the hierarchy editor, on `rebuild/hierarchy` (after #52, merged)**

- **Done:**
  - `config_get`: a config (or a schematic under the default list) as a netlist resolves it: top, view list, bindings, and every cell placed with the
    views a netlist could use, the one it gets, and the reason when none (`sim::configInfo`, which shares the config's reading with `deck()`).
  - `config_set`: top, view list, bindings (the whole map) in one undo step; makes the view when the cell has none.
  - `app/netlist_view`: a config view opens as the hierarchy editor (Top, View list, Apply, Netlist; a row per cell with **Bind to**);
    an extracted view opens read only (nets by metal, the netlist). `editor_open` and the palette take both types.
  - `ui_locate` kinds `field` (an input by its accessible name) and `option` (an entry of the open selection list), for real clicks on forms and
    drop-downs.
- **Gates:** `test_shell` (the editor offscreen), `test_simulation` (the commands), `ui_test.py` 55/55 offscreen and 57/57 with real macOS input.
- **Next:** device generators, FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices and flow runs; corners at
  transistor level; wire resistance and coupling for hand-drawn layout.

---

**Last session: Claude Code (dev), 2026-11-14 — extraction of hand-drawn layout, on `rebuild/extract` (after #51, merged)**

- **Done:**
  - `verify_extract {run}`: from an LVS run that matched, an `extracted` view of the cell. Its netlist: the transistors through their own `spice`
    templates at the W, L and diffusion areas LVS measured (fingers and multiples joined into `ng`), on the schematic's net names; a capacitor to
    ground per net from the tech LEF's `CPERSQDIST` and `EDGECAPACITANCE` per metal (`flow::Platform::techLef`).
  - LVS's deck does not name its layers: the script finds each metal's by its shapes being the drawn metal's (`layout.gds` of the run).
  - The netlister takes an `extracted` view where it took a schematic: a config's `viewList` or a binding picks it. `sim::primitiveLine` is the
    template expansion for a netlist built outside a schematic.
  - Verify panel: **Extract** on the selected LVS run.
- **Inverter (SG13G2, 1u/2u):** in 0.54 fF, out 0.79 fF; delay 13.54 ps as a schematic, 15.08 ps extracted, 16.96 ps with a 10 x 1 um Metal1 stub
  (which adds 9.951 um2 and 1.011 fF).
- **Limits:** capacitance to ground only (no R, no coupling, no shielding); a flat layout; MOS only; no global nets in the cell; no GUI yet for
  config views (they are made with `view_create` and `prop_set`) nor a viewer for an extracted view.
- **Gates:** M3 46/46, `test_simulation` (the netlister's part, no tools needed). Extract pressed with real clicks.
- **Next:** a hierarchy editor (config views in the window); device generators, FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations;
  LVS cross-probe of devices and flow runs; corners at transistor level.

---

**Last session: Claude Code (dev), 2026-11-14 — wire resistance in the transistor-level test, on `rebuild/spice-rc` (after #50, merged)**

- **Done:**
  - `test_spice` builds each net's RC network from the SPEF: a node per instance pin, the SPEF's resistors between them, its capacitors at their nodes
    (`sim/chip_deck.cpp`). `wire_resistance: false` keeps the old lumped capacitor. The record adds `wire_resistors` and `lumped_nets`.
  - A net keeps one capacitor when its network does not fit the netlist (its pins differ, or its resistors leave a node apart): never a net cut in two.
  - Coupling capacitors are grounded at their own net; the SPEF names either side first.
- **gcd, 60 ns at 10 ns:** 2,034 resistors on 533 nets, none lumped; passes, 229 s against 177 s lumped. `req_rdy`'s edge arrives 10 ps later with the resistance.
- **A failing run:** the RTL's 1 ns recording replayed on the transistors fails, 436 of 1,046 samples in 60 ns, first `req_rdy` at 15.5 ns.
  It is slow: a 1 ns clock takes about 390 steps per ns against 48 at 10 ns (39 minutes for 60 ns). The gate replays 18 ns of it.
- **Gates:** `test_chip_test` (the deck by hand), M5 with `--pdk --osdi`.
- **Next:** extraction of hand-drawn layout; device generators, FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices
  and flow runs; corners at transistor level.

---

**Last session: Claude Code (dev), 2026-11-13 — timing modes and RC derates, on `rebuild/mmmc` (after #49, merged)**

- **Done:**
  - `timing_analyze` takes `modes` (`[{name, sdc}]`; none given is one mode, `func`, the run's own constraints) and `rc` (per corner, factors on the
    extracted R and C). Every mode runs at every corner; the report's `corners` has an entry per mode and corner, and each path names its mode.
  - A derate is a copy of the run's SPEF with its `*R_UNIT`/`*C_UNIT` lines scaled (OpenSTA honours them). The open PDKs ship one extraction corner.
  - The report keeps `analysis` (what was asked). The Timing window's **Analyze** runs it again; **Setup…** is a form on `timing_analyze`.
    Forms now prefill array and object parameters.
- **gcd:** slow 3.67 ns as extracted, 3.82 ns with R and C up 20 %; fast 1.54 to 1.48 ns with both down 20 %.
- **Not done:** the chip tests' SDF (`test_run`, `test_shmoo`) is still written with the parasitics as extracted.
- **Gates:** M5 50/50 (reusing a gcd run). The Setup form was filled and Analyze pressed with real clicks.
- **Next:** RC in the transistor-level test and a real failing `test_spice`; extraction of hand-drawn layout; device generators, FinFET Pcells,
  an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices and flow runs.

---

**Last session: Claude Code (dev), 2026-11-13 — ERC, on `rebuild/erc` (after #48, merged)**

- **Done:**
  - `verify_erc`: electrical rules over an LVS report's two netlists, each flat (`run` or `flow_run`), or over a schematic alone (`library`, `cell`).
    Rules: floating gate, well with no tap, bulk on the wrong supply, net on one terminal, nodes with no path to a supply or pin, missing supply.
    A KLayout script the command writes beside the report (cached). Supplies by name (vdd/vcc/vpwr, vss/gnd), or `power`/`ground`.
  - `verify_lvs` records `erc_violations`; `flow_report`'s LVS stage too, and a run with any is not clean.
  - `verify_markers` of an LVS lists them after LVS's differences, as groups named `ERC: ...`, at the devices they name.
  - Window: the Verify panel's LVS line and details; schematic editor **Check > Electrical Rules** (Shift+X); the Implement tab's button is now
    **Show LVS and ERC findings**.
- **Known limits:** device roles come from terminal names and KLayout's classes, polarity from the class name (nmos/pmos/nfet/pfet);
  the schematic-alone check reads what LVS's reference can (SG13G2 LV MOS). No rules from the PDK's deck yet (soft connections, antenna, voltage classes).
- **Gates:** M3 34/34, M5 37/37 (reusing a gcd run: 5,134 transistors a side, clean; supplies swapped flags all 10,268 bulks). Window checked with real clicks.
- **Next:** RC per corner and multi-mode; RC in the transistor-level test and a real failing `test_spice`; extraction of hand-drawn layout;
  device generators, FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations; LVS cross-probe of devices and flow runs.

---

**Last session: Claude Code (dev), 2026-11-14 — the LVS mismatch browser, on `rebuild/lvs-debug` (after #47, merged)**

- **Done:**
  - `verify_lvs_report`: LVS's cross-reference, read from the `.lvsdb` by KLayout's own API (a script the command writes beside the report; cached).
    Each net, device, pin and subcircuit on both sides with its status, what a net joins on each side, and where it lies in the layout.
  - `verify_markers` and `layout_view_markers` take an LVS run, or `flow_run` with `check: lvs`. Markers are grouped by kind of difference.
  - The Verify panel opens an LVS that differs on its layout and says what differs. Markers panel: right-click a net, **Show in Schematic**.
  - An implementation run whose LVS differs shows **Show LVS differences**.
- **Fixed on the way:** `m3_gate.py`'s own KLayout call had no `pmap` shim on its PATH, so IHP's LVS deck crashed there.
- **Gates:** M3 26/26. The window was checked with real clicks (`os_input`): group expanded, VDD right-clicked, schematic's vdd selected.
- **Next:** ERC; RC per corner and multi-mode; RC in the transistor-level test and a real failing `test_spice`; extraction of hand-drawn layout;
  device generators, FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations.

---

**Last session: Claude Code (dev), 2026-11-13 — timing at every corner; real macOS input, on `rebuild/corners`**

- **Done:**
  - `timing_analyze` runs every corner of the platform in parallel (ihp-sg13g2 slow/typ/fast Liberty; asap7 SS/TT/FF).
  - Each path names its corner, and the report is worst-first across corners. `corners` picks some. `test_shmoo`/`test_run` take `corner`.
  - The Timing window has a Corner column.
- **gcd:** minimum period slow 3.67 ns, typ 2.32 ns, fast 1.54 ns. In simulation, slow passes from 3.54 ns and fast from 1.82 ns.
- **Real input:** `ui_test.py --backend os` passes 50/50 with Accessibility granted. It found a bug: on macOS a mouse wheel carries a pixel delta,
  so the layout and schematic canvases panned instead of zooming. Now only a scroll phase (a trackpad) pans. The Qt backend's wheel now
  sends the same as macOS, so offscreen runs catch it.
- **Gates:** M5 36/36, `ui_test.py` 50/50 (os, real window).
- **Next:** RC per corner and multi-mode; RC in the transistor-level test and a real failing `test_spice`; LVS mismatch browser and ERC;
  extraction of hand-drawn layout; device generators, FinFET Pcells, an ASAP7 LVS deck, ASAP7's DRC violations.

---

**Last session: Claude Code (dev), 2026-11-05 (cont.) — one app for RTL to GDS, then testing the chip, on `rebuild/singleapp` (OpenVLSI PR #46, open)**

- **In PR #46:**
  - RTL, Implement, Verify and Test stages; timing and power-grid sign-off, drawn on the layout.
  - Per-stage layouts; ORFS settings through `flow_run`.
  - `test_spice`: the routed chip at transistor level on TreeSPICE.
  - `test_shmoo`, `test_firmware`; a test's threads and array mode in ADE.
- **API rule (owner):** every control has a command, and `tests/test_api` enforces it. The running app serves the API; `openvlsi-mcp` attaches. `tools/openvlsi.py` is the client.
- **Real input:** `tools/ui_test.py`. `--backend os` has not run: it needs the owner to grant Accessibility permission.
- **Final tree:** ctest 25/25, M1 28/28 with the bundle, M2 35/35, M3 20/20, M5 26/26, `ui_test.py` 50/50 on a real window.
- **gcd:** STA minimum period 2.32 ns; the routed netlist passes simulation at 2.32 ns and fails at 1.39 ns; 300 ns at transistor level, 5,134 PSP transistors, 506 samples, none different, 30 minutes.
- **Not parity:** see "What runs today, tool by tool" in `docs/rebuild.md`.
- **Next, by what a designer feels first:**
  - MMMC in timing; RC in the transistor-level test; a real failing run of `test_spice`.
  - LVS mismatch browser and ERC; extraction of hand-drawn layout.
  - More device generators; FinFET Pcells and an ASAP7 LVS deck; ASAP7's 52 DRC violations.

---

**Last session: Claude Code (dev), 2026-11-05 (cont.) — build-out to the single app, on `rebuild/buildout` (OpenVLSI PR #45)**

- **Done (docs/rebuild.md, "Build-out"):** slices 1 (shell), 2 (flow commands), 4 (layout data and viewer), 5 (layout editing,
  `tools/m3_gate.py` 13 of 13 against IHP's DRC and LVS decks), 9 (ADE Verifier).
- **Next:** slice 3 (Implement stage in the GUI, on `flow_run`/`flow_status`/`flow_report`), 6 (Verify stage), 7, 8, 10, 11.
- **Open PRs for the owner:** TreeSPICE #38 (OSDI bypass; bump the pointer after it merges), OpenVLSI #45.
- **Not clean:** asap7 gcd finishes with 52 KLayout DRC violations (stock ORFS gives 34) and has no open LVS deck.
- **Not done:** the layout tools are tested through their click handlers offscreen, not real mouse events; the Verifier window
  has had no visual check.

---

**Last session: Claude Code (dev), 2026-11-05 — engine at TreeSPICE 6ef512b: MOS1 limiting and the flat transient's scale**

- **Engine (TreeSPICE #33-#37, all merged):**
  - #33: ngspice's MOS limiting; KCL at every gmin step. MOS1 chain `.op`: 9000-9500 and 10500-20000 stages converge;
    9800, 10000 and 50000 still fail at the critical gmin, where ngspice passes.
  - #34-#37: chunked worker claims, OSDI stamped as one run, kept J for bypassed devices, per-thread solver buffers;
    the matching's tie order (quadratic on supply rails); DC assembly's kept slots; Newton refines only above 1e-12.
  - BSIM4 chain at 10 threads: 2e4 transistors 12.3 -> about 7.5 s; 2e5 179 -> about 83 s.
- **OpenVLSI:** pointer only. ctest 15/15, M1 gate 24/24, M2 gate 35/35.
- **Next (owner's choice):** the MOS1 lengths that still fail; OSDI per-instance memory layout; M2 slice 7 (Spectre X).

---

**Last session: Claude Code (dev), 2026-10-30 — M2 slice 6: HB; TreeSPICE refcheck and Gear merged**

- **PRs, merge in order:** TreeSPICE `mac/hb`, then OpenVLSI `rebuild/m2-s6` (it pins the engine at `mac/hb`'s
  head; afterwards, move the pointer to its merge commit). TreeSPICE #31 (refcheck, variable-step Gear) is merged.
- **Engine:** `.hb f1 [f2 ...]` with Xyce's `.options hbint numfreq=... [selectharms=diamond intmodmax=n]`. Newton
  on the harmonics with PAC's conversion matrix as the Jacobian; several tones by the frequency map.
  - Checks: 12 Xyce HB golds (2.3e-5); a rectifier against its PSS; two tones against one at their GCD.
  - Refused: Meyer MOS (levels 1-3), BJT excess phase, transmission lines, XSPICE.
  - Also fixed: ngspice's `sin 0 1 1meg AC 1` now parses.
- **OpenVLSI:** ADE analysis `hb {tones, harms [, intmodmax]}`; the calculator's `VHB`/`IHB` and `VHBT`/`IHBT`;
  `tools/m2_gate.py` step 8 (HB against PSS and PAC on SG13G2), 35 of 35.
- **Matrix:** run_diff 1270 PASS / 0 FAIL; Xyce 208 PASS / 0 FAIL; refcheck 39/39; ctest 15/15.
- **Note:** neither build has `TREESPICE_GPU` on, and no Dawn exists here: run_diff's "gpu" mode is the CPU fallback.
- **Next:** M2 slice 7 (Spectre X); the GPU and billion-transistor assessment.

---

**Last session: Claude Code (dev), 2026-10-26 — M2 slice 5: FinFET devices (ASAP7, ASAP5)**

- **PRs, merge in order:** TreeSPICE `mac/finfet`, then OpenVLSI `rebuild/m2-s5`. The latter pins the engine at
  `mac/finfet`'s head; afterwards, move the pointer to its merge commit.
- **Owner's choice:** the PDKs' BSIM-CMG 107 cards run on a BSIM-CMG 107 OSDI, not 111.
  - It is built from Xyce's copy of the 107.0.0 Verilog-A (downloaded with permission; `BSIMCMG107_DIR` in
    `tests/build_osdi.sh`).
  - `bsimcmg107.osdi` is in `datasets/osdi` (git-ignored).
- **Engine:** LEVEL 72 routes by VERSION (107 and 111; else an error). NFIN must be whole; a CDL W must equal
  NFIN × FPITCH. The harness hands ngspice the cards as OSDI models.
- **OpenVLSI:**
  - `pdk_install asap7|asap5`: corner sections in `<pdk>.lib`; `analogLib` shared across PDKs.
  - `tools/m2_gate.py` step 7: 32 of 32.
- **Open:** Xyce's 107 inverter transient differs by 4.3 ns in its switching time; not understood.
- **Next:** M2 slice 6, HB, on the conversion matrix.

---

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
- **Next:** M2 slice 5, FinFET devices (ASAP7, ASAP5; PR #40's renumbering), then slice 6, HB, on the same conversion
  matrix. A brute-force phase-noise reference needs TRNOISE.

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
