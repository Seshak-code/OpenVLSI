#!/usr/bin/env python3
"""A real app under its API: starts OpenVLSI on a fresh workspace, attaches through openvlsi-mcp with the Python client, and draws,
selects, undoes and reads the window back through nothing but commands; then the same client works headless.

    python3 tools/api_check.py --app build/OpenVLSI.app/Contents/MacOS/OpenVLSI --mcp build/openvlsi-mcp
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


def check(ok, what):
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}", flush=True)
    if not ok:
        FAILS.append(what)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--app", required=True)
    p.add_argument("--mcp", required=True)
    a = p.parse_args()
    work = tempfile.mkdtemp(prefix="apicheck")
    libs = os.path.join(work, "openvlsi.libs")
    env = dict(os.environ, HOME=work, QT_QPA_PLATFORM=os.environ.get("QT_QPA_PLATFORM", "offscreen"))
    app = subprocess.Popen([os.path.abspath(a.app), libs], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        # Headless first would race the app for the file: wait until the app answers
        ov = None
        for _ in range(100):
            time.sleep(0.2)
            ov = openvlsi.connect(libs, mcp=os.path.abspath(a.mcp))
            if any(t["name"] == "ui_state" for t in ov.commands()):
                break
            ov.close()
            ov = None
        check(ov is not None, "openvlsi-mcp attaches to the running app: the window's commands are listed")
        if ov is None:
            return 1
        names = {t["name"] for t in ov.commands()}
        for name in ("flow_run", "test_run", "verify_drc", "layout_shape_add", "ade_run", "layout_view_tool", "editor_tool", "ui_grab"):
            check(name in names, f"{name} is there")
        info = ov.api_info()
        check(info["mode"] == "app" and info["commands"] == len(names), f"api_info: {info['mode']}, {info['commands']} commands")

        ov.library_create(library="d", path="d.oalib")
        ov.layout_tech_set(library="d", tech="sg13g2")
        ov.cell_create(library="d", cell="c")
        ov.view_create(library="d", cell="c", view="layout", type="layout")
        w = dict(library="d", cell="c", view="layout")
        ov.editor_open(**w)
        state = ov.ui_state()
        check(state["tabs"] == ["Start", "c layout"], f"the window opened the tab it was told to: {state['tabs']}")
        check(state["context"].get("cell") == "c", "and says what is active")

        ov.layout_view_current_layer(**w, layer=8, datatype=0)
        ov.layout_view_tool(**w, tool="rect")
        ov.layout_view_click(**w, x=0, y=0)
        ov.layout_view_click(**w, x=1000, y=500)
        shapes = ov.layout_objects_get(**w)["shapes"]
        check(len(shapes) == 1 and shapes[0]["layer"] == 8, "the Rectangle tool, driven by clicks, drew on Metal1")
        ov.layout_view_tool(**w, tool="select")
        ov.layout_view_click(**w, x=500, y=250)
        view = ov.layout_view_state(**w)
        check(view["selection"] == [shapes[0]["id"]], f"a click selected it: {view['selection']}")
        length = ov.layout_view_ruler(**w, x0=0, y0=0, x1=3000, y1=4000)
        check(abs(length - 5.0) < 1e-9, f"a ruler measures 5 um: {length}")
        check(len(ov.layout_view_state(**w)["rulers"]) == 1, "and is in the state")
        ov.layout_view_rulers_clear(**w)
        ov.layout_view_fit(**w)
        before = ov.layout_view_state(**w)["scale"]
        ov.layout_view_zoom(**w, factor=2)
        check(abs(ov.layout_view_state(**w)["scale"] / before - 2) < 1e-9, "zoom doubles the scale")
        ov.layout_view_delete(**w)
        check(ov.layout_objects_get(**w)["shapes"] == [], "Delete deletes the selection")
        ov.edit_undo(library="d")
        check(len(ov.layout_objects_get(**w)["shapes"]) == 1, "edit_undo brings it back")
        try:
            ov.layout_view_tool(**w, tool="label")
            check(False, "a label with no text is refused")
        except openvlsi.OpenVlsiError as e:
            check("text" in str(e), f"a refused command raises, with its reason: {e}")

        unbound = [x["text"] for x in ov.ui_actions() if not x["command"]]
        check(unbound == [], f"every control of the active tab names its command {unbound}")
        png = os.path.join(work, "window.png")
        size = ov.ui_grab(path=png)
        check(size[0] > 0 and os.path.exists(png), "ui_grab saves the window")
        ov.ui_stage(stage="test")
        check(ov.ui_state()["stage"] == "test", "ui_stage shows a stage")
        ov.ui_tab_close(title="c layout")
        check(ov.ui_state()["tabs"] == ["Start"], "ui_tab_close closes it")
        ov.ui_quit()
        ov.close()
        app.wait(timeout=20)
        check(app.returncode == 0, "ui_quit ends the app")

        with openvlsi.connect(libs, mcp=os.path.abspath(a.mcp)) as headless:
            info = headless.api_info()
            check(info["mode"] == "headless", "with the app gone, openvlsi-mcp works headless")
            check(len(headless.layout_objects_get(**w)["shapes"]) == 1, "on the same database")
            try:
                headless.ui_state()
                check(False, "the window's commands are refused")
            except openvlsi.OpenVlsiError as e:
                check("ui_state" in str(e), "the window's commands are refused there")
    finally:
        if app.poll() is None:
            app.kill()
        shutil.rmtree(work, ignore_errors=True)
    print(("PASSED" if not FAILS else f"FAILED ({len(FAILS)})"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
