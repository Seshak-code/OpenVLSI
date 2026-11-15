#!/usr/bin/env python3
"""Milestone 7's gate: a SoC built by hand from cells and libraries, through MCP alone (docs/rebuild.md, Milestone 7).

    python3 tools/m7_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--keep DIR]

It grows with the milestone's slices. Now:
1. The PDK's cells as libraries: the standard cells, IO cells and SRAM macros, each with a symbol from its CDL, its layout
   from the PDK's GDS and its CDL for LVS; those whose devices the device library has also simulate.
2. A block by hand from standard cells: an AND2 of a NAND2 and an inverter, abutted and joined on Metal2. DRC clean, LVS
   matched against the cells' own CDL (and not with the wiring wrong), extracted, and simulated through its truth table.

Needs OpenROAD-flow-scripts' ihp-sg13g2 platform (scripts/setup_eda_tools.sh) for the cells' files and KLayout for the decks.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

G_FAIL = []
G_PASS = 0


def check(ok, what):
    global G_PASS
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}", flush=True)
    if ok:
        G_PASS += 1
    else:
        G_FAIL.append(what)


class Mcp:
    def __init__(self, exe, workspace):
        self.p = subprocess.Popen([exe, "--workspace", workspace], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.n = 0
        self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "m7_gate", "version": "1"}})
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")

    def rpc(self, method, params):
        self.n += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params}) + "\n")
        self.p.stdin.flush()
        reply = json.loads(self.p.stdout.readline())
        if "error" in reply:
            raise RuntimeError(f"{method}: {reply['error']}")
        return reply["result"]

    def __call__(self, tool, **args):
        r = self.rpc("tools/call", {"name": tool, "arguments": args})
        text = r["content"][0]["text"]
        if r.get("isError"):
            raise RuntimeError(f"{tool}: {text}")
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text

    def refused(self, tool, **args):
        try:
            self(tool, **args)
            return ""
        except RuntimeError as e:
            return str(e)


def place(m, lib, cell, name, master_lib, master, x, y, nets, params=None):
    """An instance whose pins get their nets by labels on them (nets: pin -> net)."""
    m("instance_add", library=lib, cell=cell, view="schematic", master_library=master_lib, master_cell=master, name=name, x=x, y=y, params=params or {})
    for p in m("view_get", library=master_lib, cell=master, view="symbol")["pins"]:
        if p["name"] in nets:
            m("label_add", library=lib, cell=cell, view="schematic", name=nets[p["name"]], x=x + p["at"][0], y=y + p["at"][1])


def box(shape):
    xs = [p[0] for p in shape["points"]]
    ys = [p[1] for p in shape["points"]]
    return min(xs), min(ys), max(xs), max(ys)


def step_cells(m):
    print("The PDK's cells as libraries", flush=True)
    got = {}
    for kind in ("stdcell", "io", "sram"):
        got[kind] = m("pdk_install_cells", set=kind)
        r = got[kind]
        print(f"    {r['library']}: {len(r['cells'])} cells, {len(r['simulate'])} simulate", flush=True)
    std = got["stdcell"]
    check(len(std["cells"]) == 84 and set(std["simulate"]) == set(std["cells"]), f"84 standard cells, every one with a netlist that simulates: {len(std['cells'])}, {len(std['simulate'])}")
    check(len(got["io"]["cells"]) == 45 and "sg13g2_IOPadIn" in got["io"]["cells"] and len(got["io"]["simulate"]) > 20, f"45 IO cells, the pads among them: {len(got['io']['simulate'])} simulate")
    macros = [c for c in got["sram"]["cells"] if c.startswith("RM_IHPSG13_1P_")]
    check(len(macros) == 10, f"the ten SRAM macros: {macros}")
    views = {v["view"]: v["type"] for v in m("view_list", library="sg13g2_stdcell", cell="sg13g2_nand2_1")}
    pins = {p["name"]: p["direction"] for p in m("view_get", library="sg13g2_stdcell", cell="sg13g2_nand2_1", view="symbol")["pins"]}
    check(views == {"layout": "layout", "symbol": "symbol"} and pins == {"A": "input", "B": "input", "Y": "output", "VDD": "inputOutput", "VSS": "inputOutput"},
          f"a NAND2: its PDK layout and a symbol with its CDL's pins and directions: {pins}")
    cdl = m("cell_prop_get", library="sg13g2_stdcell", cell="sg13g2_nand2_1", key="cdl")
    check(".SUBCKT sg13g2_nand2_1 Y A B VDD VSS" in cdl and "sg13_lv_nmos" in cdl, "and its CDL, for LVS")
    check("already has" in m.refused("pdk_install_cells", set="stdcell"), "a set is installed once")


def step_and2(m, work):
    print("A block by hand from standard cells: an AND2", flush=True)
    sc = "sg13g2_stdcell"
    m("library_create", library="blocks", path=os.path.join(work, "blocks.oalib"))
    m("layout_tech_set", library="blocks", tech="sg13g2")
    blk = dict(library="blocks", cell="and2")
    m("cell_create", **blk)
    m("view_create", **blk, view="schematic", type="schematic")
    place(m, "blocks", "and2", "X1", sc, "sg13g2_nand2_1", 0, 0, {"A": "a", "B": "b", "Y": "n", "VDD": "vdd", "VSS": "vss"})
    place(m, "blocks", "and2", "X2", sc, "sg13g2_inv_1", 1000, 0, {"A": "n", "Y": "y", "VDD": "vdd", "VSS": "vss"})
    for k, (pin, d) in enumerate([("a", "input"), ("b", "input"), ("y", "output"), ("vdd", "inputOutput"), ("vss", "inputOutput")]):
        m("pin_add", **blk, view="schematic", name=pin, direction=d, x=-2000, y=1000 - 300 * k)
    # The layout: the cells abutted on their boundaries, so their rails join; the NAND's Y to the inverter's A on Metal2
    cells = {}
    for name in ("sg13g2_nand2_1", "sg13g2_inv_1"):
        objects = m("layout_objects_get", library=sc, cell=name)
        boundary = next(box(s) for s in objects["shapes"] if s["layer"] == 189)
        pin_shapes = [box(s) for s in objects["shapes"] if s["layer"] == 8 and s["datatype"] == 2]
        labels = {s["text"]: tuple(s["points"][0]) for s in objects["shapes"] if s["kind"] == "text"}
        cells[name] = (boundary, {t: next(b for b in pin_shapes if b[0] <= p[0] <= b[2] and b[1] <= p[1] <= b[3]) for t, p in labels.items()})
    width = cells["sg13g2_nand2_1"][0][2]
    m("view_create", **blk, view="layout", type="layout")
    m("layout_instance_add", **blk, master="sg13g2_nand2_1", masterLibrary=sc, at=[0, 0])
    m("layout_instance_add", **blk, master="sg13g2_inv_1", masterLibrary=sc, at=[width, 0])

    def rect(layer, x0, y0, x1, y1):
        return m("layout_shape_add", **blk, layer=layer, kind="rect", points=[[x0, y0], [x1, y1]])

    def via1(x, y):  # its Metal1 endcaps along y, as the cells' pins run, so it stays inside the pin
        rect("Via1", x - 95, y - 95, x + 95, y + 95)
        rect("Metal1", x - 105, y - 145, x + 105, y + 145)
        rect("Metal2", x - 150, y - 150, x + 150, y + 150)

    y_pin = cells["sg13g2_nand2_1"][1]["Y"]
    a_pin = cells["sg13g2_inv_1"][1]["A"]
    p = ((y_pin[0] + y_pin[2]) // 2 // 5 * 5, (y_pin[1] + y_pin[3]) // 2 // 5 * 5 + 600)
    q = (width + (a_pin[0] + a_pin[2]) // 2 // 5 * 5, (a_pin[1] + a_pin[3]) // 2 // 5 * 5)
    via1(*p)
    via1(*q)
    jog = [rect("Metal2", p[0] - 130, p[1] - 130, q[0] + 130, p[1] + 130), rect("Metal2", q[0] - 130, q[1] - 130, q[0] + 130, p[1] + 130)]
    nand, inv = cells["sg13g2_nand2_1"][1], cells["sg13g2_inv_1"][1]
    total = width + cells["sg13g2_inv_1"][0][2]
    for name, b in (("a", nand["A"]), ("b", nand["B"]), ("y", tuple(v + (width if k % 2 == 0 else 0) for k, v in enumerate(inv["Y"]))),
                    ("vdd", (0, nand["VDD"][1], total, nand["VDD"][3])), ("vss", (0, nand["VSS"][1], total, nand["VSS"][3]))):
        m("layout_pin_add", **blk, name=name, metal="Metal1", box=list(b))
    drc = m("verify_drc", **blk)
    rules = [r["rule"] for r in m("verify_markers", run=drc["run"])["rules"]] if not drc.get("clean") else []
    check(drc.get("clean") is True, f"IHP's DRC deck: clean {rules} {drc.get('error', '')}")
    lvs = m("verify_lvs", **blk)
    check(lvs.get("matched") is True and lvs.get("devices_layout") == lvs.get("devices_schematic") == 6 and lvs.get("erc_violations") == 0,
          f"IHP's LVS deck: the AND2 matches its schematic, six transistors a side, against the cells' own CDL {lvs.get('error', '')}")
    cdl = open(os.path.join(work, "verification", lvs["run"], "schematic.cdl")).read()
    check(".SUBCKT sg13g2_nand2_1 Y A B VDD VSS" in cdl and "XX1 n a b vdd vss sg13g2_nand2_1" in cdl, "LVS's reference holds the NAND2's CDL and places it by its ports")
    # Wired wrong: the inverter takes a instead of the NAND's output
    m("layout_objects_delete", **blk, shapes=jog)
    wrong = m("verify_lvs", **blk)
    check(wrong.get("matched") is False, "with the jog deleted, LVS no longer matches")
    m("edit_undo", library="blocks")
    ext = m("verify_extract", run=lvs["run"])
    check("X1 n a b vdd vss sg13g2_nand2_1" in ext["netlist"] and "X2 y n vdd vss sg13g2_inv_1" in ext["netlist"],
          f"verify_extract writes the two cells by their ports, with the block's own wiring: {[(n['name'], round(n['c_ff'], 3)) for n in ext['nets']]}")
    # Its truth table, from the schematic and from the extraction
    m("symbol_generate", library="blocks", cell="and2")
    tb = dict(library="blocks", cell="and2tb")
    m("cell_create", **tb)
    m("view_create", **tb, view="schematic", type="schematic")
    place(m, "blocks", "and2tb", "X1", "blocks", "and2", 0, 0, {"a": "a", "b": "b", "y": "y", "vdd": "vdd", "vss": "gnd!"})
    place(m, "blocks", "and2tb", "VDD", "analogLib", "vdc", -1500, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    place(m, "blocks", "and2tb", "VA", "analogLib", "vpulse", -2500, 0, {"PLUS": "a", "MINUS": "gnd!"}, {"v1": "0", "v2": "1.2", "td": "2n", "tr": "20p", "tf": "20p", "pw": "2n", "per": "4n"})
    place(m, "blocks", "and2tb", "VB", "analogLib", "vpulse", -3500, 0, {"PLUS": "b", "MINUS": "gnd!"}, {"v1": "0", "v2": "1.2", "td": "1n", "tr": "20p", "tf": "20p", "pw": "1n", "per": "2n"})
    got = m("config_set", **tb, view="post", top="blocks/and2tb/schematic", viewList=["extracted", "schematic", "spice"])
    uses = {c["cell"]: c.get("uses") for c in got["cells"]}
    check(uses.get("and2") == "extracted" and uses.get("sg13g2_nand2_1") == "spice", f"a config: the block's extraction, the cells' own netlists: {uses}")
    for view in ("schematic", "post"):
        run = m("simulate", **tb, view=view, cards=[".tran 5p 4n"])["run"]
        ys = [m("wave_eval", run=run, expression=f"value(VT(y), {t}n)") for t in (0.9, 1.9, 2.9, 3.9)]
        edge = m("wave_eval", run=run, expression='cross(VT(y), 0.6, 1, "rising")') - m("wave_eval", run=run, expression='cross(VT(b), 0.6, 2, "rising")')
        print(f"    {view}: y at ab = 00, 01, 10, 11: {[round(v, 2) for v in ys]}; b to y {edge * 1e12:.1f} ps", flush=True)
        check([v > 1.0 for v in ys] == [False, False, False, True] and 10e-12 < edge < 500e-12, f"simulated from its {view}, the AND2's truth table, at transistor level")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mcp", required=True)
    p.add_argument("--pdk", required=True)
    p.add_argument("--osdi", required=True)
    p.add_argument("--keep")
    a = p.parse_args()
    if not os.path.isdir(os.path.expanduser("~/eda/orfs/flow/platforms/ihp-sg13g2")) or not shutil.which("klayout", path=os.path.expanduser("~/eda/bin") + os.pathsep + os.environ.get("PATH", "")):
        print("SKIP: no ORFS ihp-sg13g2 platform or KLayout (scripts/setup_eda_tools.sh)")
        return 0
    work = os.path.abspath(a.keep) if a.keep else tempfile.mkdtemp(prefix="m7gate")
    if a.keep:
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    m = Mcp(os.path.abspath(a.mcp), os.path.join(work, "openvlsi.libs"))
    m("pdk_install", pdk="ihp-sg13g2", root=os.path.abspath(a.pdk), osdi=os.path.abspath(a.osdi))
    step_cells(m)
    step_and2(m, work)
    m.p.stdin.close()
    m.p.wait()
    print(("PASSED" if not G_FAIL else "FAILED") + f": {G_PASS} of {G_PASS + len(G_FAIL)} checks")
    return 1 if G_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
