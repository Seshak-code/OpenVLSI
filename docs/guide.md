# OpenVLSI guide

One window takes a digital design from Verilog to GDS, signs it off and tests it, and an analog one from schematic to layout. The rail on the left holds the stages, in the order a chip goes through them; Ctrl+1 to Ctrl+6 show them (Cmd on a Mac), and Cmd+K finds any command.

Everything here is also a command: [api.md](api.md) lists them all, and the last section shows how to drive the app from a script.

## Before you start

```bash
scripts/setup_eda_tools.sh    # Yosys, OpenROAD, KLayout and the ORFS platforms, into ~/eda
open build/OpenVLSI.app
```

Implement, Verify and Test use those tools. **Keep the workspace in a path without spaces:** the flow's `make` cannot handle them, and refuses with that reason. The default, `~/OpenVLSI`, is fine.

For transistor-level work, install the device library once: File > Install PDK (`pdk_install`).

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
- **Timing:** WNS, TNS, each clock's minimum period and the worst paths step by step. **Show on layout** draws a path over the design.
- **IR drop:** each rail's worst and average drop and largest grid current. **Show on layout** draws the map, red at the worst.
- **Show DRC markers** walks the violations, zooming to each.

### 5. Test the chip

A testbench on the **routed netlist** runs with the delays of its own layout. It shows the chip failing at a clock its timing cannot meet, where the RTL still passes.
- **Clock sweep** tries a testbench across periods and reports where it stops working. Write its clock as `` `PERIOD `` in the testbench.
- **Run at transistor level**, in a routed test's tab, is the closest thing to the chip: the layout's own netlist in TreeSPICE, with each net's capacitance from extraction, driven by what the testbench recorded, every output compared at the end of each clock cycle. It is slow (about 3 minutes per 60 ns of a 5,000-transistor design). Wires have capacitance here but no resistance yet.
- **Firmware** on an implemented RISC-V core: `test_firmware`, for now only as a command.

## An analog design

- **Schematic and symbol** (Design stage): the keys are Virtuoso's: I instance, W wire, L wire name, P pin, M move, Q properties, X check, Shift+E descend.
- **ADE** (Simulate stage): tests, corners, sweeps, Monte Carlo, specs; results open in the waveform viewer. A test's Acceleration column takes `threads=8` and `array=1`, which solves identical instances once.
- **Verifier:** a `verifier` view holds requirements mapped to outputs; a requirement passes only on what was measured.
- **Layout:** attach a process (`layout_tech_set`). `layout_generate` draws a layout from the schematic: its transistors in two rows, ties, pins and flight lines showing what remains to route. The keys:

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

Choose the layer to draw on in the panel. **Verify > Run DRC** and **Run LVS** check a view with the process's own decks; a DRC opens on its layout with its markers. An LVS that differs opens on its layout too: the Markers panel lists each net, device and pin that differs, named layout / schematic, and the Verify panel says what each joins on both sides. Right-click a net there and choose **Show in Schematic** to select it in the schematic.

Every LVS also checks the electrical rules (ERC) on both netlists, because a mistake the schematic and the layout share passes LVS: a gate that nothing drives, a well with no tap, a bulk on the wrong supply, a net that joins one terminal, nodes with no path to a supply or a pin, a missing supply. The run's line says how many it found, the Verify panel lists them, and the Markers panel groups them under **ERC:** at the devices they name. In a schematic, **Check > Electrical Rules** runs the same rules before any layout exists. Supplies are nets named like vdd, vcc, vpwr and vss, gnd; `verify_erc` takes other names. An implementation run counts its chip's violations in its LVS stage, and is not clean with any.

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

`--backend os` needs Accessibility permission for your terminal: System Settings > Privacy & Security > Accessibility. `ui_locate` and `ui_input` are there for your own tests.

## What it does not do yet

See "What runs today, tool by tool" in [rebuild.md](rebuild.md): it lists, against each Cadence tool, what runs and what is missing.
