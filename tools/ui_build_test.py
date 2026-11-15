#!/usr/bin/env python3
"""A chip built by hand in the window, with the mouse and the keyboard: cells from the PDK's library into a block, the block
into a top, each in schematic and layout, then DRC and LVS from the Verify stage (docs/rebuild.md, Milestone 7).

    python3 tools/ui_build_test.py --app build/OpenVLSI.app/Contents/MacOS/OpenVLSI --mcp build/openvlsi-mcp \\
        --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--backend qt|os] [--keep DIR]

Every edit goes through the window's input: menus, dialogs, clicks on the canvas, keys. The API sets up what a designer has
before they start (the device library and the standard cells) and reads back what the window made, to check it.
--backend os moves the real mouse (tools/os_input): do not touch it while it runs.
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
from ui_test import Hands, check  # noqa: E402

SC = "sg13g2_stdcell"


def form(hands, fields, ok="Run"):
    """A command's form: each field by its parameter's name, typed or chosen, then OK."""
    for name, value in fields.items():
        if isinstance(value, tuple):  # a choice: open its list, pick the entry
            hands.press("field", text=name)
            hands.press("option", text=value[0])
        else:
            hands.press("field", text=name)
            hands.key("Ctrl+A")
            hands.type(value)
    hands.press("button", text=ok)


def pick(hands, field, entry):
    hands.press("field", text=field)
    hands.press("option", text=entry)


def tree(hands, text, how="click"):
    if how == "right":  # a context menu: on the row itself, which a click first makes current
        tree(hands, text)
    x, y = hands.at("tree_item", text=text)
    if how == "double":
        hands.double(x, y)
    elif how == "right":
        if hands.backend == "os":
            hands._os("click", x, y, "right")
        else:
            hands.ov.ui_input(action="click", x=x, y=y, button="right")
        hands.settle()
        if hands.ov.ui_state()["popup"] != "QMenu":
            raise RuntimeError(f"a right click on {text!r} opened no menu (popup {hands.ov.ui_state()['popup']!r})")
    else:
        hands.click(x, y)


def reveal(ov, hands, lib, cell):
    """The cell's row in the navigator: its library expanded if need be."""
    try:
        hands.at("tree_item", text=cell)
    except Exception:
        tree(hands, lib, "double")


def in_view(hands, kind, points):
    """Fit (F), then Zoom Out ([) until every point is on the canvas, away from its edges: as a person makes room to draw."""
    win = hands.ov.ui_locate(kind="window")
    inside = lambda p: win["x"] + 480 < p[0] < win["x"] + win["width"] - 320 and win["y"] + 150 < p[1] < win["y"] + win["height"] - 120  # noqa: E731
    hands.key("f")
    for _ in range(8):
        if all(inside(hands.at(kind, x=x, y=y)) for x, y in points):
            return True
        hands.key("[")
    return False


def schematic_point(hands, x, y):
    return hands.at("editor_point", x=x, y=y)


def layout_point(hands, x, y):
    return hands.at("layout_point", x=x, y=y)


def symbol_pins(ov, lib, cell):
    return {p["name"]: tuple(p["at"]) for p in ov.view_get(library=lib, cell=cell, view="symbol")["pins"]}


def draw_schematic(ov, hands, lib, cell, parts, labels, pins):
    """parts: [(library, master, (x, y))]; labels: [(name, (x, y))] each on a pin; pins: [(name, direction, (x, y))]."""
    for master_lib, master, at in parts:
        hands.key("i")
        check(ov.ui_state()["modal"] == "Add Instance", f"I opens the instance dialog: {ov.ui_state()['modal']!r}")
        pick(hands, "Library", master_lib)
        pick(hands, "Cell", master)
        hands.press("button", text="OK")
        hands.click(*schematic_point(hands, *at))
        hands.key("Esc")
    for name, at in labels:  # one name per Wire Name, so each lands where it is clicked
        hands.key("l")
        hands.type(name)
        hands.key("Return")
        hands.click(*schematic_point(hands, *at))
        hands.key("Esc")
    for name, direction, at in pins:
        hands.key("p")
        hands.type(name)
        hands.key("Return")
        for _ in range(["input", "output", "inputOutput"].index(direction)):
            hands.key("Down")
        hands.key("Return")
        hands.click(*schematic_point(hands, *at))
        hands.key("Esc")
    got = ov.schematic_check(library=lib, cell=cell, view="schematic")
    names = sorted(n["name"] for n in got["nets"])
    return got, names


def stage(hands, name):
    """A stage's panel in view: its rail button, unless that panel shows already (a second click on it hides it)."""
    st = hands.ov.ui_state()
    if st["stage"] != name or not st["side_panel"]:
        hands.press("rail", text=name)


def new_view(ov, hands, lib, cell, view, kind):
    stage(hands, "design")  # the library navigator, whatever tab is open
    reveal(ov, hands, lib, cell)
    tree(hands, cell, "right")
    hands.press("menu_item", text="Create view…")
    form(hands, {"view": view, "type": kind})
    tree(hands, lib)  # the tree refreshes; open the view from it
    return any(v["view"] == view for v in ov.view_list(library=lib, cell=cell))


def run(ov, hands, work):
    print("A library and a block by hand", flush=True)
    lib = "mychip"
    stage(hands, "design")
    x, y = hands.at("tree_item", text=SC)
    if hands.backend == "os":
        hands._os("click", x, y, "right")
    else:
        ov.ui_input(action="click", x=x, y=y, button="right")
    hands.settle()
    hands.press("menu_item", text="Create library…")
    form(hands, {"library": lib, "path": os.path.join(work, lib + ".oalib")})
    check(lib in ov.workspace_libraries(), "Create library… makes it")
    tree(hands, lib, "right")
    hands.press("menu_item", text="Attach Process…")
    form(hands, {"tech": ("sg13g2",)})
    check(ov.layout_tech_get(library=lib).get("name") == "sg13g2", "Attach Process… gives it SG13G2")
    tree(hands, lib, "right")
    hands.press("menu_item", text="Create cell…")
    form(hands, {"cell": "and2"})
    check("and2" in ov.cell_list(library=lib), "Create cell… makes and2")
    tree(hands, lib)
    check(new_view(ov, hands, lib, "and2", "schematic", "schematic"), "Create view… makes its schematic")
    tree(hands, "and2 > schematic", "double")
    check(ov.ui_state()["active_title"] == "and2 schematic", f"a double click opens it: {ov.ui_state()['active_title']}")

    nand, inv = symbol_pins(ov, SC, "sg13g2_nand2_1"), symbol_pins(ov, SC, "sg13g2_inv_1")
    at_nand, at_inv = (0, 0), (1000, 0)
    on = lambda origin, p: (origin[0] + p[0], origin[1] + p[1])  # noqa: E731
    labels = [("a", on(at_nand, nand["A"])), ("b", on(at_nand, nand["B"])), ("y", on(at_inv, inv["Y"])),
              ("vdd", on(at_nand, nand["VDD"])), ("vdd", on(at_inv, inv["VDD"])), ("vss", on(at_nand, nand["VSS"])), ("vss", on(at_inv, inv["VSS"]))]
    pins = [("a", "input", (-1500, 600)), ("b", "input", (-1500, 300)), ("y", "output", (-1500, 0)), ("vdd", "inputOutput", (-1500, -300)), ("vss", "inputOutput", (-1500, -600))]
    hands.key("i")  # the first instance, where the empty canvas is
    pick(hands, "Library", SC)
    pick(hands, "Cell", "sg13g2_nand2_1")
    hands.press("button", text="OK")
    hands.click(*schematic_point(hands, *at_nand))
    hands.key("Esc")
    in_view(hands, "editor_point", [(-1600, 700), (1300, -700)])
    got, names = draw_schematic(ov, hands, lib, "and2", [(SC, "sg13g2_inv_1", at_inv)], [], [])
    check(len(ov.view_get(library=lib, cell="and2", view="schematic")["instances"]) == 2, "I, a library, a cell and a click place the NAND2 and the inverter")
    # The wire between them: NAND's Y to the inverter's A
    in_view(hands, "editor_point", [(-1600, 700), (1300, -700)])
    hands.key("w")
    hands.click(*schematic_point(hands, *on(at_nand, nand["Y"])))
    hands.double(*schematic_point(hands, *on(at_inv, inv["A"])))
    hands.key("Esc")
    got, names = draw_schematic(ov, hands, lib, "and2", [], labels, pins)
    nets = {n["name"]: sorted((t.get("instance", ""), t["pin"]) for t in n["terminals"]) for n in got["nets"]}
    joined = [k for k, v in nets.items() if ("I0", "Y") in v or any(t[1] == "Y" and t[0] for t in v)]
    check(not got["errors"] and set(names) >= {"a", "b", "y", "vdd", "vss"} and len(nets) == 6,
          f"the wire, the labels and the pins make its six nets, with no errors: {names} {got['errors'][:2]}")
    hands.menu("Create", "Symbol from Schematic")
    check("symbol" in [v["view"] for v in ov.view_list(library=lib, cell="and2")], "Create > Symbol from Schematic makes its symbol")

    print("Its layout by hand", flush=True)
    check(new_view(ov, hands, lib, "and2", "layout", "layout"), "Create view… makes its layout")
    tree(hands, "and2 > layout", "double")
    w = dict(library=lib, cell="and2", view="layout")
    ov.ui_console(visible=False)
    for master, x in (("sg13g2_nand2_1", 0), ("sg13g2_inv_1", 1925)):  # the second near the first's edge: it abuts
        hands.key("i")
        pick(hands, "Library", SC)
        pick(hands, "Cell", master)
        hands.press("button", text="OK")
        hands.click(*layout_point(hands, x, 0))
        hands.key("Esc")
        in_view(hands, "layout_point", [(-300, -300), (3700, 4300)])
    placed = {i["cell"]: (i["at"], i.get("library")) for i in ov.layout_objects_get(**w)["instances"]}
    check(placed.get("sg13g2_nand2_1") == ([0, 0], SC) and placed.get("sg13g2_inv_1") == ([1920, 0], SC),
          f"Instance… places the standard cells from their library, abutted where clicked: {placed}")
    # The NAND's Y to the inverter's A: Via1 on each pin from Metal1, then Metal2 between them
    p, q = (960, 2600), (2385, 1685)
    tree(hands, "Metal1")
    hands.key("o")
    hands.click(*layout_point(hands, *p))
    hands.click(*layout_point(hands, *q))
    hands.key("Esc")
    tree(hands, "Metal2")
    hands.key("r")
    hands.click(*layout_point(hands, p[0] - 130, p[1] - 130))
    hands.click(*layout_point(hands, q[0] + 130, p[1] + 130))
    hands.click(*layout_point(hands, q[0] - 130, q[1] - 130))
    hands.click(*layout_point(hands, q[0] + 130, p[1] + 130))
    hands.key("Esc")
    # Its pins on Metal1, over the cells' own
    tree(hands, "Metal1")
    for name, (x0, y0, x1, y1) in (("a", (1270, 1470, 1600, 1900)), ("b", (330, 1470, 620, 1900)), ("y", (2775, 610, 3005, 3175)),
                                   ("vdd", (0, 3560, 3360, 4000)), ("vss", (0, -220, 3360, 220))):
        hands.key("Ctrl+P")
        hands.key("Ctrl+A")
        hands.type(name)
        hands.key("Return")
        hands.click(*layout_point(hands, x0, y0))
        hands.click(*layout_point(hands, x1, y1))
        hands.key("Esc")
    texts = sorted(s["text"] for s in ov.layout_objects_get(**w)["shapes"] if s["kind"] == "text")
    check(texts == ["a", "b", "vdd", "vss", "y"], f"Pin… and two clicks make each pin: {texts}")

    print("Checked from the Verify stage", flush=True)
    results = verify(ov, hands, lib, "and2")
    check(results.get("drc", {}).get("clean") is True, f"Run DRC…: clean ({results.get('drc', {}).get('violations')} violations)")
    lvs = results.get("lvs", {})
    check(lvs.get("matched") is True and lvs.get("devices_layout") == 6, f"Run LVS…: the hand-drawn AND2 matches its hand-drawn schematic {lvs.get('error', '')}")

    print("A top of two blocks, by hand", flush=True)
    tree(hands, lib, "right")
    hands.press("menu_item", text="Create cell…")
    form(hands, {"cell": "top"})
    tree(hands, lib)
    new_view(ov, hands, lib, "top", "schematic", "schematic")
    tree(hands, "top > schematic", "double")
    blk = symbol_pins(ov, lib, "and2")
    a1, a2 = (0, 0), (2000, 0)
    draw_schematic(ov, hands, lib, "top", [(lib, "and2", a1)], [], [])
    in_view(hands, "editor_point", [(-2600, 1000), (2600, -700)])
    draw_schematic(ov, hands, lib, "top", [(lib, "and2", a2)], [], [])
    in_view(hands, "editor_point", [(-2600, 1000), (2600, -700)])
    hands.key("w")
    hands.click(*schematic_point(hands, *on(a1, blk["y"])))
    hands.double(*schematic_point(hands, *on(a2, blk["a"])))
    hands.key("Esc")
    labels = [("a", on(a1, blk["a"])), ("b1", on(a1, blk["b"])), ("b2", on(a2, blk["b"])), ("y", on(a2, blk["y"])),
              ("vdd", on(a1, blk["vdd"])), ("vdd", on(a2, blk["vdd"])), ("vss", on(a1, blk["vss"])), ("vss", on(a2, blk["vss"]))]
    pins = [("a", "input", (-2500, 900)), ("b1", "input", (-2500, 600)), ("b2", "input", (-2500, 300)), ("y", "output", (-2500, 0)),
            ("vdd", "inputOutput", (-2500, -300)), ("vss", "inputOutput", (-2500, -600))]
    got, names = draw_schematic(ov, hands, lib, "top", [], labels, pins)
    check(not got["errors"] and len(ov.view_get(library=lib, cell="top", view="schematic")["instances"]) == 2, f"two AND2 blocks, chained by a wire: {names} {got['errors'][:2]}")

    new_view(ov, hands, lib, "top", "layout", "layout")
    tree(hands, "top > layout", "double")
    t = dict(library=lib, cell="top", view="layout")
    for x in (0, 3365):
        hands.key("i")
        pick(hands, "Library", lib)
        pick(hands, "Cell", "and2")
        hands.press("button", text="OK")
        hands.click(*layout_point(hands, x, 0))
        hands.key("Esc")
        in_view(hands, "layout_point", [(-300, -300), (7000, 4300)])
    placed = sorted(i["at"][0] for i in ov.layout_objects_get(**t)["instances"])
    check(placed == [0, 3360], f"the second block, placed near the first, abuts it: {placed}")
    # The first block's y to the second's a: Via1 on each, Metal2 below the blocks' own jogs
    p, q = (2890, 1000), (3360 + 1435, 1685)
    tree(hands, "Metal1")
    hands.key("o")
    hands.click(*layout_point(hands, *p))
    hands.click(*layout_point(hands, *q))
    hands.key("Esc")
    tree(hands, "Metal2")
    hands.key("r")
    hands.click(*layout_point(hands, p[0] - 130, p[1] - 130))
    hands.click(*layout_point(hands, q[0] + 130, p[1] + 130))
    hands.click(*layout_point(hands, q[0] - 130, p[1] - 130))
    hands.click(*layout_point(hands, q[0] + 130, q[1] + 130))
    hands.key("Esc")
    tree(hands, "Metal1")
    for name, (x0, y0, x1, y1) in (("a", (1270, 1470, 1600, 1900)), ("b1", (330, 1470, 620, 1900)), ("b2", (3360 + 330, 1470, 3360 + 620, 1900)),
                                   ("y", (3360 + 2775, 610, 3360 + 3005, 3175)), ("vdd", (0, 3560, 6720, 4000)), ("vss", (0, -220, 6720, 220))):
        hands.key("Ctrl+P")
        hands.key("Ctrl+A")
        hands.type(name)
        hands.key("Return")
        hands.click(*layout_point(hands, x0, y0))
        hands.click(*layout_point(hands, x1, y1))
        hands.key("Esc")
    results = verify(ov, hands, lib, "top")
    check(results.get("drc", {}).get("clean") is True, f"Run DRC… on the top: clean ({results.get('drc', {}).get('violations')} violations)")
    lvs = results.get("lvs", {})
    check(lvs.get("matched") is True, f"Run LVS… on the top: two hand-built blocks and the wire between them match, level by level {lvs.get('error', '')}")
    unbound = [x["text"] for x in ov.ui_actions() if not x["command"]]
    check(unbound == [], f"every control there names its command {unbound}")
    check(ov.ui_input_dropped() == 0, f"no input went nowhere: {ov.ui_input_dropped()} dropped")


def verify(ov, hands, lib, cell):
    """Run DRC… and Run LVS… from the Verify stage, with the cell's layout the active tab; their records."""
    out = {}
    before = {r["run"] for r in ov.verify_list()}
    stage(hands, "verify")
    for kind, button in (("drc", "Run DRC…"), ("lvs", "Run LVS…")):
        hands.press("button", text=button)
        hands.press("button", text="Run")
        for _ in range(600):
            runs = [r for r in ov.verify_list() if r["run"] not in before and r.get("kind") == kind and r.get("cell") == cell]
            if runs and runs[-1].get("state") != "running":
                out[kind] = runs[-1]
                break
            time.sleep(0.5)
    stage(hands, "design")
    return out


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
    if a.backend == "os" and subprocess.run([a.os_input, "trusted"], capture_output=True, text=True).stdout.strip() != "yes":
        print("macOS has not given this terminal Accessibility permission, so its events would not arrive.")
        return 3
    work = os.path.abspath(a.keep) if a.keep else tempfile.mkdtemp(prefix="uibuild")
    if a.keep:
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    libs = os.path.join(work, "openvlsi.libs")
    # What a designer has before they start: the device library and the PDK's standard cells
    setup = openvlsi.connect(libs, mcp=a.mcp)
    setup.pdk_install(pdk="ihp-sg13g2", root=a.pdk, osdi=a.osdi)
    setup.pdk_install_cells(set="stdcell")
    setup.close()
    # The app runs with this workspace as its home (its settings stay here); the tools it finds under ~/eda go with it
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
