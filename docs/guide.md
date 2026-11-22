# OpenVLSI guide

One window takes a digital design from Verilog to GDS, signs it off and tests it, and an analog one from schematic to layout. The rail on the left holds the stages, in the order a chip goes through them; Ctrl+1 to Ctrl+6 show them (Cmd on a Mac), and Cmd+K finds any command.

Everything here is also a command: [api.md](api.md) lists them all, and the last section shows how to drive the app from a script.

## Menus and forms

The menu bar follows a Cadence tool's: **File** (new library, cell or view, Open Cellview, GDS import and export, the PDK's installs), the active tool's own menus, **Tools** (Library Manager, the command palette and window, DRC, LVS, a schematic as Verilog), **Options** (Appearance, EDA Tools, Keyboard Shortcuts), **Window** (the side panel, the stages) and **Help** (this guide, the command reference, About). Every item names the command it runs (`ui_actions`).

Every form is its command's parameters. Right-click a field for the values it can take (the workspace's libraries, a library's cells, a cell's views, runs, platforms, layers) or Browse… for a file or folder; typing completes from the same list. A yes/no field left blank takes the command's default.

## Characterizing a cell

**Tools > Characterize Cell…** (`lib_characterize`) makes a standard cell's Liberty file on TreeSPICE, as Liberate does: its function from a
truth table, then every timing arc's delay and output transition over input transitions and output loads, with inputs driven through a
predriver as characterization tools drive them, and each input's capacitance. The indices are the PDK's Liberty's for the cell at the
corner (`slow`, `typ`, `fast`), and every value is compared with it: the result lists each table's mean and worst difference.

```tcl
set r [lib_characterize -library sg13g2_stdcell -cell sg13g2_nand2_1 -corner typ]
dict get $r liberty
```

Combinational cells only, for now; give `slews` and `loads` for other indices, `predriver ramp` for a linear input.

## Tcl and Python consoles

**Tools > Tcl Console** is a Tcl 9 interpreter inside the app, as in Innovus: every command is a Tcl command taking `-param value`, and
returns dicts and lists. `help` lists the commands, `help cell_list` describes one.

```tcl
foreach c [cell_list -library analog] { puts "$c: [view_list -library analog -cell $c]" }
set run [dict get [flow_run -design gcd -verilog [list /abs/gcd.v] -top gcd -platform sky130hd -clock_period_ns 5] run]
```

**Tools > Python Console** is your own `python3`, its `ov` this app (every command a method; `ov.commands()` lists them). Your
installed packages are there, and blocks continue on `...` as in python's own prompt.

```python
for c in ov.cell_list(library="analog"):
    print(c, ov.view_list(library="analog", cell=c))
```

Either way the windows follow each call, and Up recalls the last lines. `console_eval {language, code}` runs code in either from a script
or an agent. Set `OPENVLSI_PYTHON` for another interpreter.

## Before you start

```bash
scripts/setup_eda_tools.sh    # Yosys, OpenROAD, KLayout and the ORFS platforms, into ~/eda
open build/OpenVLSI.app
```

Implement, Verify and Test use those tools. **Keep the workspace in a path without spaces:** the flow's `make` cannot handle them, and refuses with that reason. The default, `~/OpenVLSI`, is fine.

For transistor-level work, install the device library once: File > Install PDK (`pdk_install`). To build from the PDK's own cells, then File > Install PDK Cells (`pdk_install_cells`): `stdcell` (84 standard cells), `io` (pads and their parts) or `sram` (the SRAM macros). Each cell has the PDK's layout, a symbol, its CDL for LVS and, for the standard cells, a netlist that simulates. Place them in a schematic and in a layout like your own cells; LVS compares against their CDL.

A whole chip by hand: in its top layout, Create > Pad Ring… (`layout_pad_ring`: the pads of each side, a die size), Seal Ring… (`layout_seal_ring`) and, once the core is placed and wired, Fill… (`layout_fill`, about a minute). Wire each pad's core pin (`p2c` of an input pad, `c2p` of an output pad, on Metal2 at its inner edge) to your blocks, and strap the core supplies to the ring's innermost rails (vdd on Metal3, vss on Metal4). Whole-chip DRC is `verify_drc` with `density` (after fill) and `recommended` off (IHP's own pads break two recommended rules); LVS compares the pads and SRAM macros by their pins.

To test a chip built by hand, write its top schematic as Verilog (`netlist_verilog`: it returns the file and the models it needs) and run your testbench on them with Test > Run (`test_run`, level rtl, `verilog` the file and models). `tools/rv32i` holds a worked RISC-V chip: its core, firmware, power grid and testbench.

A design the flow built becomes a block to place by hand: **Make Block** in its run's tab (`flow_make_macro`). It gets the run's layout (on the standard-cell library's cells, so a chip holds one copy of each), a symbol from its Verilog ports, its netlist for LVS and one for simulation. A cell with no schematic, as a block, is checked by LVS against its own netlist.

LVS on IHP's deck joins grounds through the substrate, but not supplies: each NWell is its own, so a cell's VDD must be wired to the supply it shares, or be a pin of its own.

## From Verilog to a tested chip

### 1. RTL

**New design** records your Verilog, top module, clock and platform. Double-click a file to open it. **Check** compiles the design and lists problems; click one to jump to its line.

### 2. Simulate the RTL

In the **Test** stage, **New test** takes a testbench of yours. It passes only if it:
- finishes by itself (a time limit stops one that hangs),
- prints its pass text (`PASS` by default),
- prints no line starting `FAIL`, `ERROR` or `FATAL`.

So a silent testbench does not pass. Every signal is recorded, and the run's tab shows the waveforms: click to place the cursor, scroll to zoom.

### 3. Implement

**Implement** in the editor, or **New run** in the Implement stage, runs synthesis, floorplan, placement, clock tree, routing, DRC and LVS. The tab shows:
- each stage's progress and metrics, with the log beneath;
- the verdict. "Finished" does not mean clean: **Clean** requires timing met, no DRC violations, LVS matched and no electrical rule violations, and otherwise every reason is listed;
- the layout at any stage: click a finished stage in the pipeline. **Open layout** shows the final GDS.

Settings beyond clock and utilization are any ORFS variable, as `PLACE_DENSITY=0.6` in the run's Settings field.

### 4. Sign off

From a run's tab:
- **Timing:** WNS, TNS, each clock's minimum period and the worst paths step by step, at every corner of the platform. **Show on layout** draws a path over the design. **Setup…** takes more:
  - `modes`: other constraints for the same chip, as `[{"name": "func"}, {"name": "scan", "sdc": "/abs/scan.sdc"}]`. Every mode runs at every corner, and each path names its mode.
  - `rc`: what to multiply the extracted resistance and capacitance by at a corner, as `{"slow": {"r": 1.15, "c": 1.1}}`. The open PDKs extract one RC corner, so this is how the wires get corners; the factors are yours to choose.
  - **Analyze** runs the last setup again.
- **IR drop:** each rail's worst and average drop and largest grid current. **Show on layout** draws the map, red at the worst.
- **Show DRC markers** walks the violations, zooming to each.

### 5. Test the chip

A testbench on the **routed netlist** runs with the delays of its own layout. It shows the chip failing at a clock its timing cannot meet, where the RTL still passes.
- **Clock sweep** tries a testbench across periods and reports where it stops working. Write its clock as `` `PERIOD `` in the testbench.
- **Run at transistor level**, in a routed test's tab, is the closest thing to the chip: the layout's own netlist in TreeSPICE, with each net's wire resistance and capacitance from extraction (its RC network, a node per pin), driven by what the testbench recorded, every output compared at the end of each clock cycle. It is slow (about 4 minutes per 60 ns of a 5,000-transistor design at a 10 ns clock; a 1 ns clock takes ten times that). A recording the chip cannot keep up with fails, and says the first output and time that differ: run it on an RTL test's tab to see where the real chip parts from the RTL. The box beside the button picks a corner: **slow**, **typ** or **fast** run the transistors at the MOS models, supply and temperature of the timing corner of that name (on SG13G2: 1.08 V and 125 °C, 1.2 V and 25 °C, 1.32 V and −40 °C); **nominal** is the typical models at the platform's supply and 27 °C.
- **Firmware** on an implemented RISC-V core: `test_firmware`, for now only as a command.

## An analog design

- **Schematic and symbol** (Design stage): the keys are Virtuoso's: I instance, W wire, L wire name, P pin, M move, Q properties, X check, Shift+E descend.
- **ADE** (Simulate stage): tests, corners, sweeps, Monte Carlo, specs; results open in the waveform viewer. A test's Acceleration column takes `threads=8` and `array=1`, which solves identical instances once.
- **Verifier:** a `verifier` view holds requirements mapped to outputs; a requirement passes only on what was measured.
- **Layout:** attach a process (`layout_tech_set`). `layout_generate` draws a layout from the schematic: its transistors in two rows, its poly resistors (rsil, rppd, rhigh) and MIM capacitors (cap_cmim: Metal5 below, TopMetal1 above) beside the n-row, ties, pins and flight lines showing what remains to route. **Create > Transistor…**, **Resistor…** (Shift+R) and **Capacitor…** (Shift+C) generate one device of the size you type and put it on the cursor to place. The keys:

| Key | Does | Key | Does |
|---|---|---|---|
| R | Rectangle | M | Move |
| P | Path (Enter ends) | C | Copy |
| Shift+P | Polygon | S | Stretch an edge |
| O | Via, up from the current metal | Del | Delete |
| L | Label | U, Shift+U | Undo, redo |
| Ctrl+P | Pin | K, Shift+K | Ruler, clear |
| I, Shift+I | Instance, transistor | F | Fit |
| Esc | End the tool | Shift+F, Ctrl+F | All levels, top only |

Choose the layer to draw on in the panel. **Verify > Run DRC** and **Run LVS** check a view with the process's own decks; a DRC opens on its layout with its markers. An LVS that differs opens on its layout too: the Markers panel lists each net, device and pin that differs, named layout / schematic, and the Verify panel says what each joins on both sides. Right-click a net or a device there and choose **Show in Schematic** to select it in the schematic.

Every LVS also checks the electrical rules (ERC) on both netlists, because a mistake the schematic and the layout share passes LVS: a gate that nothing drives, a well with no tap, a bulk on the wrong supply, a net that joins one terminal, nodes with no path to a supply or a pin, a missing supply. The run's line says how many it found, the Verify panel lists them, and the Markers panel groups them under **ERC:** at the devices they name. In a schematic, **Check > Electrical Rules** runs the same rules before any layout exists. Supplies are nets named like vdd, vcc, vpwr and vss, gnd; `verify_erc` takes other names. An implementation run counts its chip's violations in its LVS stage, and is not clean with any.

A layout may place other cells, from any library; LVS then compares it with its schematic level by level (each schematic cell a subcircuit), and says which cell differs. A cell below the top takes its supplies through its pins, as its layout does: a global net (`vdd!`) is the top's alone.

**Extract**, with an LVS run that matched selected, writes the layout's own netlist as an `extracted` view of the cell (`verify_extract`): its transistors at the sizes and diffusion areas LVS measured, on the schematic's net names, and a capacitor to ground on every net for its wiring, from the process's own area and edge values per metal. To simulate the layout in place of the schematic, give the test bench a config view (right-click the cell, **Create view…**, type `config`) and open it: the **hierarchy editor**. It offers the cell's schematic as the top; set the view list to `extracted schematic spice` and **Apply**. The table lists every cell the hierarchy places with the view it gets; **Bind to** puts one cell on a view of its own ahead of the list, and **Netlist** shows what the config simulates. Run that view (`simulate`, or as an ADE test's design). An extracted view opens too, read only: each net's capacitance by metal, and its netlist. It is capacitance only: no wire resistance or coupling yet. A hierarchy is extracted cell by cell: extract each cell from its own LVS run; a parent's view holds its own wiring and its subcells, each of which the config resolves to its own extraction if it has one.

## Driving it from outside

Everything above is a command, in three ways in:

```bash
# MCP: attaches to the app if it has this workspace open, so the window follows
openvlsi-mcp --workspace ~/OpenVLSI/openvlsi.libs
```

```python
# Python: every command is a method (PYTHONPATH=tools; OPENVLSI_MCP=build/openvlsi-mcp)
import openvlsi
ov = openvlsi.connect("~/OpenVLSI/openvlsi.libs")
run = ov.flow_run(design="gcd", verilog=["/abs/gcd.v"], top="gcd", platform="ihp-sg13g2", clock_period_ns=10)
print(ov.flow_report(run=run["run"])["clean"])
print(ov.timing_analyze(run=run["run"])["clocks"])
```

```text
# The command window (Ctrl+J), or OpenVLSI --run LINE
flow_list {}
```

`api_list` returns every command with its schema, and `ui_actions` says which command each control in the active tab is. A command that refuses says why.

### Testing the window itself

`tools/ui_test.py` clicks menus, draws and types, then checks the database and the pixels on screen:

```bash
python3 tools/ui_test.py --app build/OpenVLSI.app/Contents/MacOS/OpenVLSI --mcp build/openvlsi-mcp               # through Qt's window-system queue
python3 tools/ui_test.py --app build/OpenVLSI.app/Contents/MacOS/OpenVLSI --mcp build/openvlsi-mcp --backend os  # real macOS events; takes over the mouse
```

`tools/ui_rv32i_test.py` (same arguments as the next) builds the RISC-V chip of `tools/rv32i` in the window, from New run… to its firmware's test, in about 25 minutes. `tools/ui_build_test.py` (same arguments, plus `--pdk` and `--osdi`) builds a block and a top of two blocks with the mouse alone, through DRC and LVS. A run with `--backend os` shows on screen; leave the mouse and keyboard alone, and keep the screen unlocked (macOS does not deliver events to a locked screen).

`--backend os` needs Accessibility permission for your terminal: System Settings > Privacy & Security > Accessibility. `ui_locate` and `ui_input` are there for your own tests.

## What it does not do yet

See "What runs today, tool by tool" in [rebuild.md](rebuild.md): it lists, against each Cadence tool, what runs and what is missing.
