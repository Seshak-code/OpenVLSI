#!/usr/bin/env python3
"""The window under real input: menus clicked, shapes drawn with the mouse, keys pressed, each checked against the database and,
with a GPU, against the pixels on screen. Everything is located through ui_locate, so it clicks what a person would see.

    python3 tools/ui_test.py --app build/OpenVLSI.app/Contents/MacOS/OpenVLSI --mcp build/openvlsi-mcp [--backend qt|os] [--os-input build/os_input] [--keep DIR]

--backend qt: input enters Qt's window-system queue, where the platform's own events enter. Needs no permission; runs offscreen in CI.
--backend os: real macOS events from outside the app (tools/os_input), which take over the mouse while it runs: do not touch it. macOS asks
for Accessibility permission for the terminal; without it this reports that and exits 3. A real window and GPU, so the pixels are checked too.
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

FAILS = []
PASSES = 0

# macOS virtual key codes
KEYCODES = {"a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9, "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17, "o": 31, "u": 32, "i": 34, "p": 35,
            "l": 37, "j": 38, "k": 40, "n": 45, "m": 46, "Return": 36, "Esc": 53, "Backspace": 51}


def check(ok, what):
    global PASSES
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}", flush=True)
    if ok:
        PASSES += 1
    else:
        FAILS.append(what)


class Hands:
    """One way to act on the window, whichever backend delivers it."""

    def __init__(self, ov, backend, os_input):
        self.ov, self.backend, self.os_input = ov, backend, os_input

    def settle(self):
        for _ in range(200):
            if self.ov.ui_input_pending() == 0:
                break
            time.sleep(0.01)
        time.sleep(0.15 if self.backend == "os" else 0.03)
        for _ in range(100):  # what the events started
            if self.ov.ui_input_pending() == 0:
                return
            time.sleep(0.01)

    def _os(self, *args):
        subprocess.run([self.os_input, *[str(a) for a in args]], check=True)

    def click(self, x, y, modifiers=()):
        if self.backend == "os":
            if modifiers:
                raise RuntimeError("os_input clicks without modifiers")
            self._os("click", x, y)
        else:
            self.ov.ui_input(action="click", x=x, y=y, modifiers=list(modifiers))
        self.settle()

    def double(self, x, y):
        if self.backend == "os":
            self._os("double", x, y)
        else:
            self.ov.ui_input(action="double_click", x=x, y=y)
        self.settle()

    def drag(self, x0, y0, x1, y1):
        if self.backend == "os":
            self._os("drag", x0, y0, x1, y1)
        else:
            self.ov.ui_input(action="drag", x=x0, y=y0, x2=x1, y2=y1)
        self.settle()

    def wheel(self, x, y, delta):
        if self.backend == "os":
            self._os("wheel", x, y, delta)
        else:
            self.ov.ui_input(action="wheel", x=x, y=y, delta=delta)
        self.settle()

    def key(self, name):
        if self.backend == "os":
            parts = name.split("+")
            mods = [{"Shift": "shift", "Ctrl": "cmd", "Meta": "ctrl", "Alt": "alt"}[p] for p in parts[:-1]]  # Qt's Ctrl is macOS's Command
            self._os("key", KEYCODES[parts[-1].lower() if len(parts[-1]) == 1 else parts[-1]], *mods)
        else:
            self.ov.ui_input(action="key", key=name)
        self.settle()

    def at(self, kind, **where):
        r = self.ov.ui_locate(kind=kind, **where)
        return r["cx"], r["cy"]

    def press(self, kind, **where):
        self.click(*self.at(kind, **where))

    def menu(self, title, *items):
        self.press("menu", text=title)
        for item in items:
            self.press("menu_item", text=item)


def run(ov, hands, gpu, work):
    print("Menus and the layout tools", flush=True)
    ov.library_create(library="d", path="d.oalib")
    ov.layout_tech_set(library="d", tech="sg13g2")
    ov.cell_create(library="d", cell="c")
    ov.view_create(library="d", cell="c", view="layout", type="layout")
    w = dict(library="d", cell="c", view="layout")
    ov.editor_open(**w)
    ov.ui_console(visible=False)
    ov.layout_view_window(**w, x0=0, y0=0, x1=10, y1=10)  # microns: a known scale
    state = lambda: ov.layout_view_state(**w)  # noqa: E731
    shapes = lambda: ov.layout_objects_get(**w)["shapes"]  # noqa: E731
    box = lambda s: (min(p[0] for p in s["points"]), min(p[1] for p in s["points"]), max(p[0] for p in s["points"]), max(p[1] for p in s["points"]))  # noqa: E731
    tol = 2 / state()["scale"] + 5  # two pixels, and the grid

    hands.press("tree_item", text="Metal2")
    check(state()["current_layer"] == "10/0", f"a click in the layer panel chooses Metal2: {state().get('current_layer')}")

    hands.press("menu", text="Create")
    check(ov.ui_state()["popup"] == "QMenu", "a click on Create opens its menu")
    hands.press("menu_item", text="Rectangle")
    check(ov.ui_state()["popup"] == "" and state()["tool"] == "rect", "a click on Rectangle starts the tool and closes the menu")

    hands.click(*hands.at("layout_point", x=2000, y=2000))
    check(len(shapes()) == 0, "one click draws nothing yet")
    hands.click(*hands.at("layout_point", x=6000, y=5000))
    made = shapes()
    check(len(made) == 1 and made[0]["layer"] == 10, "the second click draws it, on Metal2")
    if made:
        b = box(made[0])
        check(all(abs(a - e) <= tol for a, e in zip(b, (2000, 2000, 6000, 5000))), f"where the mouse went: {b}")
        check(all(v % 5 == 0 for v in b), "on the manufacturing grid")
    if gpu:
        inside = ov.ui_pixel(x=hands.at("layout_point", x=4000, y=3500)[0], y=hands.at("layout_point", x=4000, y=3500)[1])
        outside = ov.ui_pixel(x=hands.at("layout_point", x=8500, y=8500)[0], y=hands.at("layout_point", x=8500, y=8500)[1])
        check(inside != outside, f"the screen shows it: {inside} inside, {outside} outside")
    hands.key("Esc")
    check(state()["tool"] == "select", "Esc ends the tool")

    hands.click(*hands.at("layout_point", x=4000, y=3500))
    check(state()["selection"] == [made[0]["id"]], f"a click on it selects it: {state()['selection']}, wanted {made[0]['id']}; {state()['status']}, tool {state()['tool']}")
    hands.click(*hands.at("layout_point", x=9000, y=9000))
    check(state()["selection"] == [], "a click on nothing clears the selection")
    hands.click(*hands.at("layout_point", x=4000, y=3500))

    hands.key("m")
    check(state()["tool"] == "move", "M starts Move")
    hands.click(*hands.at("layout_point", x=4000, y=3500))
    hands.click(*hands.at("layout_point", x=5000, y=4500))
    moved = box(shapes()[0])
    check(abs(moved[0] - 3000) <= 2 * tol and abs(moved[1] - 3000) <= 2 * tol, f"two clicks move it by their distance: {moved}")

    before = state()["window"]
    x0, y0 = hands.at("layout_point", x=8500, y=1000)
    hands.drag(x0, y0, x0 + 60, y0 - 40)
    after = state()["window"]
    check(after != before and len(shapes()) == 1, "a drag on the canvas pans, drawing nothing")
    scale = state()["scale"]
    hands.wheel(*hands.at("layout_point", x=5000, y=5000), 480)
    check(state()["scale"] != scale, f"the wheel zooms: scale {scale:.4f} to {state()['scale']:.4f}")
    ov.layout_view_window(**w, x0=0, y0=0, x1=10, y1=10)

    hands.click(*hands.at("layout_point", x=5000, y=4500))
    hands.menu("Edit", "Delete")
    check(shapes() == [], "Edit > Delete deletes the selection")
    hands.key("u")
    check(len(shapes()) == 1, "U undoes it")
    hands.key("Shift+U")
    check(shapes() == [], "Shift+U redoes it")
    hands.key("u")

    hands.key("p")
    check(state()["tool"] == "path", "P starts Path")
    for x, y in ((1000, 7000), (4000, 7000), (4000, 9000)):
        hands.click(*hands.at("layout_point", x=x, y=y))
    hands.key("Return")
    path = [s for s in shapes() if s["kind"] == "path"]
    check(len(path) == 1 and len(path[0]["points"]) == 3 and path[0]["width"] == 200, "three clicks and Return make a path at the metal's width")

    hands.key("Esc")
    hands.click(*hands.at("layout_point", x=1000, y=7000))
    selected = state()["selection"]
    hands.key("Backspace")
    check(selected and not [s for s in shapes() if s["kind"] == "path"], "Backspace deletes too")
    hands.key("u")

    hands.key("Shift+P")
    for x, y in ((7000, 7000), (9000, 7000)):
        hands.click(*hands.at("layout_point", x=x, y=y))
    hands.double(*hands.at("layout_point", x=9000, y=9000))
    polygons = [s for s in shapes() if s["kind"] == "polygon" and s["points"][0][1] > 6000]
    check(len(polygons) == 1, f"a double click closes a polygon: {[(x['kind'], x['points'][:4]) for x in shapes()]}, tool {state()['tool']}: {state()['status']}")
    hands.key("Esc")

    hands.key("k")
    hands.click(*hands.at("layout_point", x=1000, y=1000))
    hands.click(*hands.at("layout_point", x=4000, y=1000))
    rulers = state()["rulers"]
    check(len(rulers) == 1 and abs(abs(rulers[0][2] - rulers[0][0]) - 3000) <= 2 * tol, f"K and two clicks measure 3 um: {rulers}")
    hands.key("Shift+K")
    check(state()["rulers"] == [], "Shift+K clears it")

    print("The shell", flush=True)
    for stage in ("rtl", "verify", "implement", "test", "simulate", "design"):
        hands.press("rail", text=stage)
        check(ov.ui_state()["stage"] == stage, f"a click on the rail shows {stage}")
    hands.press("console")
    check(ov.ui_state()["console"] is True, "the rail's button opens the command window")
    hands.press("console")
    hands.menu("Window", "Appearance", "Light")
    check(ov.ui_state()["appearance"] == "light", "Window > Appearance > Light, through a submenu")
    if gpu:
        canvas_light = ov.ui_pixel(x=hands.at("layout_point", x=9500, y=500)[0], y=hands.at("layout_point", x=9500, y=500)[1])
    hands.menu("Window", "Appearance", "Dark")
    check(ov.ui_state()["appearance"] == "dark", "and back to Dark")
    if gpu:
        canvas_dark = ov.ui_pixel(x=hands.at("layout_point", x=9500, y=500)[0], y=hands.at("layout_point", x=9500, y=500)[1])
        check(canvas_light != canvas_dark, f"the canvas really changes: {canvas_light}, {canvas_dark}")

    hands.press("rail", text="implement")
    hands.press("button", text="New run…")
    check(ov.ui_state()["modal"] == "New implementation run", f"New run opens its dialog: {ov.ui_state()['modal']!r}")
    hands.press("button", text="Run")
    check(ov.ui_state()["modal"] == "New implementation run", "Run with nothing filled in keeps the dialog open, with why")
    hands.press("button", text="Cancel")
    check(ov.ui_state()["modal"] == "", "Cancel closes it")
    check(ov.flow_list() == [], "and started nothing")

    print("Verilog, by hand", flush=True)
    src = os.path.join(work, "blink.v")
    with open(src, "w") as f:
        f.write("module blink(input clk, output reg q);\n  always @(posedge clk) q <= ~q;\nendmodule\n")
    ov.rtl_design_set(design="blink", verilog=[src], top="blink")
    hands.press("rail", text="rtl")
    hands.press("tree_item", text="blink.v")
    hands.double(*hands.at("tree_item", text="blink.v"))
    check(ov.ui_state()["active_title"] == "blink.v", f"a double click on a file opens it: {ov.ui_state()['active_title']}")
    hands.press("button", text="Check")
    check(ov.rtl_view_state(file=src)["status"] == "No errors", f"Check says so: {ov.rtl_view_state(file=src)['status']}")
    ov.rtl_view_text(file=src, text="module blink(input clk, output reg q);\n  always @(posedge clk) q <= ~r;\nendmodule\n")
    check(ov.rtl_view_state(file=src)["modified"] is True, "an edit is unsaved")
    hands.press("button", text="Check")
    st = ov.rtl_view_state(file=src)
    check(st["modified"] is False and st["problems"] >= 1 and "error" in st["status"], f"Check saves it and lists the mistake: {st}")
    hands.key("Ctrl+S")
    ov.ui_tab_close(title="blink.v")
    hands.press("tab", text="c layout")

    print("A schematic, by hand", flush=True)
    ov.view_create(library="d", cell="c", view="schematic", type="schematic")
    sch = dict(library="d", cell="c", view="schematic")
    ov.editor_open(**sch)
    ov.editor_fit(**sch)
    hands.key("w")
    check(ov.editor_state(**sch)["prompt"].startswith("Wire"), f"W starts a wire, the tab having the keyboard as soon as it opens: {ov.editor_state(**sch)['prompt']!r}, focus {ov.ui_state()['focus']}")
    for x, y in ((-200, 100), (200, 100)):
        hands.click(*hands.at("editor_point", x=x, y=y))
    hands.double(*hands.at("editor_point", x=200, y=-100))
    wires = ov.view_get(**sch)["wires"]
    check(len(wires) == 1 and wires[0]["points"] == [[-200, 100], [200, 100], [200, -100]], f"clicks and a double click draw it, snapped: {wires}")
    hands.key("Esc")
    hands.press("tab", text="c layout")
    check(ov.ui_state()["active_title"] == "c layout", "a click on a tab switches to it")
    hands.press("tab_close", text="c schematic")
    check("c schematic" not in ov.ui_state()["tabs"], "its close button closes it")
    unbound = [x["text"] for x in ov.ui_actions() if not x["command"]]
    check(unbound == [], f"and every control there names its command {unbound}")
    check(ov.ui_input_dropped() == 0, f"no input went nowhere: {ov.ui_input_dropped()} dropped")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--app", required=True)
    p.add_argument("--mcp", required=True)
    p.add_argument("--backend", default="qt", choices=["qt", "os"])
    p.add_argument("--os-input", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "os_input"))
    p.add_argument("--keep")
    a = p.parse_args()
    if a.backend == "os":
        if not os.path.exists(a.os_input):
            print("os_input is not built: it is macOS only")
            return 3
        if subprocess.run([a.os_input, "trusted"], capture_output=True, text=True).stdout.strip() != "yes":
            print("macOS has not given this terminal Accessibility permission, so its events would not arrive.\n"
                  "Add it in System Settings > Privacy & Security > Accessibility, then run this again.")
            return 3
    work = os.path.abspath(a.keep) if a.keep else tempfile.mkdtemp(prefix="uitest")
    if a.keep:
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    libs = os.path.join(work, "openvlsi.libs")
    env = dict(os.environ, HOME=work)
    gpu = env.get("QT_QPA_PLATFORM") != "offscreen"
    app = subprocess.Popen([os.path.abspath(a.app), "--window-menus", libs], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ov = None
        for _ in range(100):
            time.sleep(0.2)
            ov = openvlsi.connect(libs, mcp=os.path.abspath(a.mcp))
            if any(t["name"] == "ui_input" for t in ov.commands()):
                break
            ov.close()
            ov = None
        if ov is None:
            print("FAILED: the app did not answer")
            return 1
        ov.ui_raise()
        time.sleep(0.5)
        run(ov, Hands(ov, a.backend, os.path.abspath(a.os_input)), gpu, work)
        ov.ui_quit()
        ov.close()
        app.wait(timeout=20)
    finally:
        if app.poll() is None:
            app.kill()
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    print(("PASSED" if not FAILS else "FAILED") + f": {PASSES} of {PASSES + len(FAILS)} checks ({a.backend}, {'a real window' if gpu else 'offscreen'})")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
