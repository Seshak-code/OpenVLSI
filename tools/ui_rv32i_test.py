#!/usr/bin/env python3
"""The RISC-V chip built in the window, with the mouse and the keyboard (docs/rebuild.md, Milestone 7; the gate's step 4 by hand):
the RV32I core with its firmware ROM and an SRAM macro through the flow (Implement: New run…), made a block (Make Block); a chip
library, its top's schematic of 16 pads and the block (instances, labels, pins); its layout: Pad Ring…, the block placed, Route >
Pads and Supplies, Seal Ring…, Fill…; DRC and LVS from the Verify stage; its firmware run from the schematic (Test: New test…).

    python3 tools/ui_rv32i_test.py --app build/OpenVLSI.app/Contents/MacOS/OpenVLSI --mcp build/openvlsi-mcp \\
        --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--backend qt|os] [--keep DIR]

The API sets up what a designer has before they start (the PDK's libraries, the design's sources) and reads back what the window
made, to check it. --backend os moves the real mouse (tools/os_input): do not touch it while it runs. About 25 minutes.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import openvlsi  # noqa: E402
import ui_test  # noqa: E402
from ui_build_test import form, in_view, new_view, pick, reveal, stage, tree  # noqa: E402
from ui_test import Hands, check  # noqa: E402

FLOW = os.path.expanduser("~/eda/orfs/flow")
SRAM = "RM_IHPSG13_1P_64x64_c2_bm_bist"
W, H = 1600, 1400
PADS = ([("clk", "sg13g2_IOPadIn", "clk_core"), ("reset", "sg13g2_IOPadIn", "reset_core")] +
        [("uart_d%d" % k, "sg13g2_IOPadOut4mA", "ud%d" % k) for k in range(8)] +
        [("uart_valid", "sg13g2_IOPadOut4mA", "valid_core"), ("halted", "sg13g2_IOPadOut4mA", "halted_core"), ("vdd", "sg13g2_IOPadVdd", None),
         ("vss", "sg13g2_IOPadVss", None), ("iovdd", "sg13g2_IOPadIOVdd", None), ("iovss", "sg13g2_IOPadIOVss", None)])
SIDES = {"south": ["clk", "reset"], "north": ["uart_d%d" % k for k in range(8)], "east": ["uart_valid", "halted", "vdd", "vss"], "west": ["iovdd", "iovss"]}


def wait_for(what, done, seconds):
    for _ in range(int(seconds / 2)):
        got = done()
        if got:
            return got
        time.sleep(2)
    raise RuntimeError("waited " + str(seconds) + " s for " + what)


def place_instance(hands, lib, cell, point, kind="editor_point"):
    """I, the library and the cell from the dialog, a click where it goes."""
    hands.key("i")
    pick(hands, "Library", lib)
    pick(hands, "Cell", cell)
    hands.press("button", text="OK")
    hands.click(*hands.at(kind, x=point[0], y=point[1]))
    hands.key("Esc")


def zoom_on(hands, point, units_per_px=3, kind="editor_point"):
    """Fit (F), then the wheel in at the point until a pixel is a few units: as a person zooms in to click exactly."""
    hands.key("f")
    for _ in range(30):
        a, b = hands.at(kind, x=point[0], y=point[1]), hands.at(kind, x=point[0] + 300, y=point[1])
        if abs(b[0] - a[0]) >= 300 / units_per_px:
            return
        hands.wheel(*a, 120)


RETRIED = []


def label(hands, name, point):
    """L, its name, a click on the pin; once more if the click placed none (printed: a lost click is worth knowing)."""
    for attempt in range(2):
        before = len(hands.ov.view_get(library="chip", cell="rv32i_chip", view="schematic")["labels"])
        hands.key("l")
        hands.type(name)
        hands.key("Return")
        hands.click(*hands.at("editor_point", x=point[0], y=point[1]))
        hands.key("Esc")
        if len(hands.ov.view_get(library="chip", cell="rv32i_chip", view="schematic")["labels"]) > before:
            return
        RETRIED.append((name, point))
        print(f"    a click placed no label {name} at {point}: again", flush=True)


def pin(hands, name, direction, point):
    hands.key("p")
    hands.type(name)
    hands.key("Return")
    for _ in range(["input", "output", "inputOutput"].index(direction)):
        hands.key("Down")
    hands.key("Return")
    hands.click(*hands.at("editor_point", x=point[0], y=point[1]))
    hands.key("Esc")


def run(ov, hands, work):
    rtl = os.path.join(work, "rtl")
    platform = os.path.join(FLOW, "platforms/ihp-sg13g2")

    print("The core through the flow, from the Implement stage", flush=True)
    stage(hands, "implement")
    hands.press("button", text="New run…")
    hands.press("field", text="Design")
    hands.type("soc_core")
    src = os.path.join(FLOW, "designs/src/riscv32i")
    sources = [os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".v") and f not in ("top.v", "rom.v", "dmem.v")]
    sources += [os.path.join(rtl, f) for f in ("soc_core.v", "firmware_rom.v", "sram_blackbox.v")]
    for f in sources:
        hands.press("field", text="Source path")
        hands.type(f)
        hands.key("Return")
    form(hands, {"Top module": "soc_core", "Clock period": "40",
                 "Settings": f"ADDITIONAL_LEFS={platform}/lef/{SRAM}.lef ADDITIONAL_LIBS={platform}/lib/{SRAM}_typ_1p20V_25C.lib "
                             f"ADDITIONAL_GDS={platform}/gds/{SRAM}.gds ADDITIONAL_CDL={platform}/cdl/{SRAM}.cdl "
                             f'DIE_AREA="0 0 860 580" CORE_AREA="20 20 840 560" MACRO_PLACE_HALO="10 10" '
                             f"PDN_TCL={rtl}/pdn.tcl IO_CONSTRAINTS={rtl}/io.tcl"})
    runs = [r for r in ov.flow_list() if r["run"].startswith("soc_core/")]
    check(len(runs) == 1, f"New run… starts the core's run with its {len(sources)} sources and its macro's settings: {[r['run'] for r in runs]}")
    flow = runs[0]["run"]
    st = wait_for("the flow", lambda: (lambda s: s if s["state"] in ("done", "failed", "stopped") else None)(ov.flow_status(run=flow)), 3600)
    report = ov.flow_report(run=flow)
    check(st["state"] == "done" and report.get("clean") is True, f"the run goes from RTL to GDS, DRC clean, LVS matched: {report.get('not_clean')} {st.get('error', '')}")
    hands.press("button", text="Make Block")
    wait_for("the block", lambda: "soc_core" in ov.workspace_libraries() and "soc_core" in ov.cell_list(library="soc_core"), 300)
    check(any(i["cell"] == SRAM for i in ov.layout_objects_get(library="soc_core", cell="soc_core")["instances"]),
          "Make Block makes the core a block, its SRAM macro placed from sg13g2_sram")

    print("The chip's schematic by hand", flush=True)
    stage(hands, "design")
    x, y = hands.at("tree_item", text="sg13g2_stdcell")
    if hands.backend == "os":
        hands._os("click", x, y, "right")
    else:
        ov.ui_input(action="click", x=x, y=y, button="right")
    hands.settle()
    hands.press("menu_item", text="Create library…")
    form(hands, {"library": "chip", "path": os.path.join(work, "chip.oalib")})
    tree(hands, "chip", "right")
    hands.press("menu_item", text="Attach Process…")
    form(hands, {"tech": ("sg13g2",)})
    tree(hands, "chip", "right")
    hands.press("menu_item", text="Create cell…")
    form(hands, {"cell": "rv32i_chip"})
    tree(hands, "chip")
    new_view(ov, hands, "chip", "rv32i_chip", "schematic", "schematic")
    tree(hands, "rv32i_chip > schematic", "double")
    symbols = {cell: {p["name"]: p["at"] for p in ov.view_get(library="sg13g2_io", cell=cell, view="symbol")["pins"]} for cell in {c for _, c, _ in PADS}}
    block = {p["name"]: p["at"] for p in ov.view_get(library="soc_core", cell="soc_core", view="symbol")["pins"]}
    xs = [1000 * k for k in range(len(PADS))]
    in_view(hands, "editor_point", [(-3500, -600), (xs[-1] + 600, 6600)])
    for k, (name, cell, core) in enumerate(PADS):
        place_instance(hands, "sg13g2_io", cell, (xs[k], 0))
    place_instance(hands, "soc_core", "soc_core", (0, 5000))
    # The chip's pins first, left of the pads: then no pad sits at the edge of the fitted view, where zooming in on it would leave
    # its outer pin off the canvas
    in_view(hands, "editor_point", [(-3500, -600), (16000, 6600)])
    for k, (name, cell, core) in enumerate(PADS):
        pin(hands, name, "input" if name in ("clk", "reset") else "output" if core else "inputOutput", (-3000, 400 * k))
    # Each instance where it landed (a click snaps to the grid); the labels on their pins, zoomed in on each
    placed = sorted(ov.view_get(library="chip", cell="rv32i_chip", view="schematic")["instances"], key=lambda i: int(i["name"][1:]))
    sup = {"vdd": "vdd", "vss": "vss", "iovdd": "iovdd", "iovss": "iovss"}
    for k, (name, cell, core) in enumerate(PADS):
        x0, y0 = placed[k]["at"]
        zoom_on(hands, (x0, y0))
        nets = dict(sup, pad=name, p2c=core, c2p=core)
        for p, at in symbols[cell].items():
            if nets.get(p):
                label(hands, nets[p], (x0 + at[0], y0 + at[1]))
    to = {"clk": "clk_core", "reset": "reset_core", "uart_valid": "valid_core", "halted": "halted_core", "VDD": "vdd", "VSS": "vss"}
    to.update({"uart_data[%d]" % k: "ud%d" % k for k in range(8)})
    bx, by = placed[len(PADS)]["at"]
    for p, at in block.items():
        zoom_on(hands, (bx + at[0], by + at[1]))
        label(hands, to[p], (bx + at[0], by + at[1]))
    hands.key("f")
    got = ov.schematic_check(library="chip", cell="rv32i_chip", view="schematic")
    sch = ov.view_get(library="chip", cell="rv32i_chip", view="schematic")
    unnamed = [n["name"] for n in got.get("nets", []) if n["name"].startswith("net")]
    check(len(sch["instances"]) == 17 and len(sch["pins"]) == 16 and not got.get("errors") and not unnamed and not RETRIED,
          f"I, L and P place the 16 pads and the core, name their nets and make the chip's pins: {len(sch['instances'])} instances, {len(got.get('nets', []))} nets, "
          f"unnamed {unnamed}, clicks again {RETRIED}, {got.get('errors')}")

    print("The chip's layout by hand", flush=True)
    new_view(ov, hands, "chip", "rv32i_chip", "layout", "layout")
    tree(hands, "rv32i_chip > layout", "double")
    side = lambda names: "[" + ", ".join('{"name": "%s", "master": "%s"}' % (n, dict((p[0], p[1]) for p in PADS)[n]) for n in names) + "]"  # noqa: E731
    hands.menu("Create", "Pad Ring…")
    form(hands, {"die": f"[{W}, {H}]", "south": side(SIDES["south"]), "east": side(SIDES["east"]), "north": side(SIDES["north"]), "west": side(SIDES["west"])})
    lay = dict(library="chip", cell="rv32i_chip")
    insts = ov.layout_objects_get(**lay)["instances"]
    check(sum(i["cell"].startswith("sg13g2_IOPad") for i in insts) == 16, f"Create > Pad Ring… places the 16 pads, corners and fillers: {len(insts)} instances")
    in_view(hands, "layout_point", [(0, 0), (W * 1000, H * 1000)])
    place_instance(hands, "soc_core", "soc_core", (370000, 400000), "layout_point")
    blk = [i for i in ov.layout_objects_get(**lay)["instances"] if i["cell"] == "soc_core"]
    check(len(blk) == 1 and abs(blk[0]["at"][0] - 370000) < 20000 and abs(blk[0]["at"][1] - 400000) < 20000,
          f"Instance… places the core where it is clicked: {blk[0]['at'] if blk else None}")
    hands.menu("Route", "Pads and Supplies")
    wires = [s for s in ov.layout_objects_get(**lay)["shapes"] if s["layer"] in (10, 30, 126, 134)]
    check(len(wires) > 100, f"Route > Pads and Supplies wires each pad to the core's pin its schematic names, and the supplies to its rings: {len(wires)} shapes")
    hands.menu("Create", "Seal Ring…")
    form(hands, {"die": f"[{W}, {H}]"})
    hands.menu("Create", "Fill…")
    hands.press("button", text="Run")
    cells = ov.cell_list(library="chip")
    check(f"sealring_{W}x{H}" in cells and "rv32i_chip__fill" in cells, "Create > Seal Ring… and Fill… frame and fill the die")

    print("Checked from the Verify stage", flush=True)
    before = {r["run"] for r in ov.verify_list()}
    stage(hands, "verify")
    hands.press("button", text="Run DRC…")
    pick(hands, "density", "true")       # the whole chip, filled
    pick(hands, "recommended", "false")  # IHP's own pads break two
    hands.press("button", text="Run")
    hands.press("button", text="Run LVS…")
    hands.press("button", text="Run")
    done = {}
    for kind in ("drc", "lvs"):
        done[kind] = wait_for(kind, lambda: next((r for r in ov.verify_list() if r["run"] not in before and r.get("kind") == kind and r.get("cell") == "rv32i_chip"
                                                  and r.get("state") != "running"), None), 1800)
    drc, lvs = done["drc"], done["lvs"]
    rules = [(r["rule"], r["count"]) for r in ov.verify_markers(run=drc["run"])["rules"]] if not drc.get("clean") else []
    check(drc.get("density") is True and all(r == "Seal.m" for r, _ in rules),
          f"Run DRC…: the whole chip clean with density and latch-up rules, but for Seal.m (as on IHP's own seal ring) {rules}")
    check(lvs.get("matched") is True and lvs.get("erc_violations") == 0, f"Run LVS…: the chip matches its schematic, no ERC finding {lvs.get('error', '')}")

    print("Its firmware, from the Test stage", flush=True)
    stage(hands, "test")
    hands.press("button", text="New test…")
    if ov.ui_state()["modal"] != "New test":
        print("    New test… opened no dialog:", {k: ov.ui_state()[k] for k in ("modal", "popup", "active_title", "stage")}, flush=True)
    hands.press("field", text="Testbench path")
    hands.type(os.path.join(rtl, "tb_chip.v"))
    hands.key("Return")
    pick(hands, "What runs", "A chip, from its schematic")
    ihp = os.path.expanduser("~/eda/ihp-verilog/RM_IHPSG13_1P_core_behavioral_bm_bist.v")
    form(hands, {"Its top module": "tb", "Chip": "chip/rv32i_chip", "Defines": "FUNCTIONAL",
                 "More Verilog": "" if os.path.isfile(ihp) else os.path.join(rtl, "sram_core_model.v"), "Time limit": "400000"})
    test = wait_for("the test", lambda: next((t for t in reversed(ov.test_list()) if t.get("name") == "tb_chip" and t.get("level") == "rtl"
                                                and t.get("state") not in ("running", None)), None), 900)
    check(test.get("passed") is True, f"New test… runs the firmware on the chip: Hello, world! at its pads {test.get('error', '')}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--app", required=True)
    p.add_argument("--mcp", required=True)
    p.add_argument("--pdk", required=True)
    p.add_argument("--osdi", required=True)
    p.add_argument("--backend", default="qt", choices=["qt", "os"])
    p.add_argument("--os-input", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "os_input"))
    p.add_argument("--keep")
    a = p.parse_args()
    a.app, a.mcp, a.os_input, a.pdk, a.osdi = (os.path.abspath(x) for x in (a.app, a.mcp, a.os_input, a.pdk, a.osdi))
    if not os.path.isdir(os.path.join(FLOW, "platforms/ihp-sg13g2")):
        print("SKIP: no ORFS ihp-sg13g2 platform (scripts/setup_eda_tools.sh)")
        return 0
    if a.backend == "os" and subprocess.run([a.os_input, "trusted"], capture_output=True, text=True).stdout.strip() != "yes":
        print("macOS has not given this terminal Accessibility permission, so its events would not arrive.")
        return 3
    work = os.path.abspath(a.keep) if a.keep else tempfile.mkdtemp(prefix="uirv32i")
    if " " in work:
        print("the work directory must have no space (ORFS's make): " + work)
        return 3
    if a.keep:
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    libs = os.path.join(work, "openvlsi.libs")
    # What a designer has before they start: the PDK's libraries, and the design's sources (core, firmware, power grid, pins, testbench)
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rv32i")
    rtl = os.path.join(work, "rtl")
    os.makedirs(rtl)
    for f in os.listdir(here):
        shutil.copy(os.path.join(here, f), rtl)
    subprocess.run([sys.executable, os.path.join(here, "firmware.py"), rtl], check=True)
    setup = openvlsi.connect(libs, mcp=a.mcp)
    setup.pdk_install(pdk="ihp-sg13g2", root=a.pdk, osdi=a.osdi)
    for kind in ("stdcell", "io", "sram"):
        setup.pdk_install_cells(set=kind)
    setup.close()
    if os.path.isdir(os.path.expanduser("~/eda")) and not os.path.exists(os.path.join(work, "eda")):
        os.symlink(os.path.expanduser("~/eda"), os.path.join(work, "eda"))
    env = dict(os.environ, HOME=work, PATH=os.path.expanduser("~/eda/bin") + os.pathsep + os.environ.get("PATH", ""))
    app = subprocess.Popen([a.app, "--window-menus", libs], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ov = None
        for _ in range(100):
            time.sleep(0.2)
            ov = openvlsi.connect(libs, mcp=a.mcp)
            if any(t["name"] == "ui_input" for t in ov.commands()):
                break
            ov.close()
            ov = None
        if ov is None:
            print("FAILED: the app did not answer")
            return 1
        ov.ui_raise()
        time.sleep(0.5)
        run(ov, Hands(ov, a.backend, a.os_input), work)
        check(ov.ui_input_dropped() == 0, f"no input went nowhere: {ov.ui_input_dropped()} dropped")
        ov.ui_quit()
        ov.close()
        app.wait(timeout=20)
    finally:
        if app.poll() is None:
            app.kill()
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    print(("PASSED" if not ui_test.FAILS else "FAILED") + f": {ui_test.PASSES} of {ui_test.PASSES + len(ui_test.FAILS)} checks ({a.backend})")
    return 1 if ui_test.FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
