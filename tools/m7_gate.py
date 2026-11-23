#!/usr/bin/env python3
"""Milestone 7's gate: a SoC built by hand from cells and libraries, through MCP alone (docs/rebuild.md, Milestone 7).

    python3 tools/m7_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--keep DIR]

It grows with the milestone's slices. Now:
1. The PDK's cells as libraries: the standard cells, IO cells and SRAM macros, each with a symbol from its CDL, its layout
   from the PDK's GDS and its CDL for LVS; those whose devices the device library has also simulate.
2. A block by hand from standard cells: an AND2 of a NAND2 and an inverter, abutted and joined on Metal2. DRC clean, LVS
   matched against the cells' own CDL (and not with the wiring wrong), extracted, and simulated through its truth table.
3. A chip by hand: the AND2 in a pad ring (layout_pad_ring), each pad wired to the block on Metal2 and Metal3, the supplies
   strapped to the ring's rails, IHP's seal ring around it and IHP's fill inside. DRC clean with density and latch-up (not the
   recommended rules, which IHP's own pads break), LVS matched with the pads compared as black boxes by their LEF pins, and not
   when a wire is missing or two pads trade places. The seal ring is IHP's own, XORed against ORFS's example. Its test: the top
   schematic as Verilog (netlist_verilog), run through the pads' and standard cells' models (test_run): its truth table at the pads.
4. A RISC-V chip by hand: an RV32I core (ORFS's riscv32i), its firmware ROM and an SRAM macro (tools/rv32i) through the flow, made
   a block; 16 pads wired by hand, seal ring, fill; DRC and LVS over the whole chip; its firmware's "Hello, world!" at the pads.
5. A flow-built block as a hard macro: gcd through the flow, made a cell (its standard cells placed from sg13g2_stdcell); LVS
   against its own netlist; a top in a third library placing it and a standard cell, LVS matched as one hierarchy; the block
   simulated at transistor level out of reset.

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


# Characterization against IHP's Liberty: each table's mean, its worst point (always at the 1 fF column, 3 to 10 % high there, within
# 1 to 3 % elsewhere), input pins (an inverter's within 0.1 %; a multi-input cell's 3 to 13 % low: IHP's averaging is not ours), a path
LIB_MEAN, LIB_WORST, LIB_CAP, LIB_PATH = 0.03, 0.12, 0.15, 0.03


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
    abstract = m("cell_prop_get", library="sg13g2_io", cell="sg13g2_IOPadIn", key="abstract")
    check(sorted({p["name"] for p in abstract}) == ["iovdd", "iovss", "p2c", "pad", "vdd", "vss"] and "bondpad_70x70" in m("cell_list", library="sg13g2_io"),
          "an IO cell's abstract for LVS, its LEF pins; the bond pad is a cell of the library")


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


def step_chip(m, work):
    print("A chip by hand: the AND2 block in a pad ring, wired to its pads", flush=True)
    m("library_create", library="die", path=os.path.join(work, "die.oalib"))
    m("layout_tech_set", library="die", tech="sg13g2")
    top = dict(library="die", cell="and2chip")
    m("cell_create", **top)
    m("view_create", **top, view="layout", type="layout")
    P = lambda n, k: {"name": n, "master": k}  # noqa: E731
    D = 1400  # um: the core big enough against the pad ring for IHP's global GatPoly density once filled
    ring = m("layout_pad_ring", **top, die=[D, D], south=[P("a", "sg13g2_IOPadIn"), P("b", "sg13g2_IOPadIn")], north=[P("y", "sg13g2_IOPadOut4mA")],
             east=[P("vdd", "sg13g2_IOPadVdd"), P("vss", "sg13g2_IOPadVss")], west=[P("iovdd", "sg13g2_IOPadIOVdd"), P("iovss", "sg13g2_IOPadIOVss")])
    check(ring["pads"] == 7 and ring["core"] == [320000, 320000, D * 1000 - 320000, D * 1000 - 320000], f"layout_pad_ring: seven pads, corners and {ring['fillers']} fillers around a {D - 640} um core")
    pads = sorted((i["at"], i["angle"]) for i in m("layout_objects_get", **top)["instances"] if i["cell"].startswith("sg13g2_IOPad"))
    south = [at for at, angle in pads if angle == 0]   # a, b, left to right
    north = [at for at, angle in pads if angle == 180]  # y
    blk = m("layout_objects_get", library="blocks", cell="and2")
    pins = {}
    for t in (s for s in blk["shapes"] if s["kind"] == "text"):
        x, y = t["points"][0]
        pins[t["text"]] = next(box(s) for s in blk["shapes"] if s["layer"] == 8 and s["datatype"] == 2 and box(s)[0] <= x <= box(s)[2] and box(s)[1] <= y <= box(s)[3])
    total = max(b[2] for b in pins.values())
    O = (D * 500 - 20000, D * 500 - 20000)
    m("layout_instance_add", **top, master="and2", masterLibrary="blocks", at=list(O))

    def rect(layer, x0, y0, x1, y1):
        return m("layout_shape_add", **top, layer=layer, kind="rect", points=[[min(x0, x1), min(y0, y1)], [max(x0, x1), max(y0, y1)]])

    def via(metal, x, y):
        return m("layout_via_add", **top, **{"from": metal}, at=[x, y])

    def via1(x, y):  # onto a cell's Metal1 pin: its Metal1 endcaps along the pin
        rect("Via1", x - 95, y - 95, x + 95, y + 95)
        rect("Metal1", x - 105, y - 145, x + 105, y + 145)
        rect("Metal2", x - 200, y - 200, x + 200, y + 200)

    def wire_in(pad_x, pin, below):  # from an input pad's p2c (Metal2, 39.83 to 40.12 um in), across on Metal3 below the block, up onto the pin
        x = pad_x + 39975
        X, Y = O[0] + (pin[0] + pin[2]) // 2 // 5 * 5, O[1] + (pin[1] + pin[3]) // 2 // 5 * 5
        R = O[1] - below
        rect("Metal2", pad_x + 39830, 318000, pad_x + 40120, R + 145)
        via("Metal2", x, R)
        across = rect("Metal3", x, R - 200, X, R + 200)
        via("Metal2", X, R)
        rect("Metal2", X - 100, R, X + 100, Y)
        via1(X, Y)
        return across

    wire_in(south[0][0], pins["a"], 2000)
    b_across = wire_in(south[1][0], pins["b"], 3000)
    # y: from the output pad's c2p (Metal2, 38.145 to 41.855 um in, R180) down, across above the block, onto the inverter's Y below the
    # block's own Metal2
    yp = pins["y"]
    cx = north[0][0] - 40000
    X, Y, Yr = O[0] + (yp[0] + yp[2]) // 2 // 5 * 5, O[1] + yp[1] + 300, O[1] + 8000
    rect("Metal2", cx - 500, D * 1000 - 318000, cx + 500, Yr - 145)
    rect("Metal2", cx - 145, Yr - 300, cx + 145, Yr + 145)
    via("Metal2", cx, Yr)
    rect("Metal3", cx, Yr - 200, X, Yr + 200)
    via("Metal2", X, Yr)
    rect("Metal2", X - 100, Y, X + 100, Yr)
    via1(X, Y)
    # Supplies: each Metal1 rail carried past the block, stacked up and strapped east to the ring's innermost rail on that metal
    # (Metal3's is vdd, Metal4's vss: 160 to 178 um in from the ring's outer edge)
    for name, metal in (("vdd", "Metal3"), ("vss", "Metal4")):
        r = pins[name]
        yc = O[1] + (r[1] + r[3]) // 2 // 5 * 5
        xs = O[0] + total + 1500
        rect("Metal1", O[0] + total - 200, O[1] + r[1], xs + 400, O[1] + r[3])
        for lower in ("Metal1", "Metal2", "Metal3")[: 2 if metal == "Metal3" else 3]:
            via(lower, xs, yc)
            if lower != "Metal1":
                rect(lower, xs - 250, yc - 250, xs + 250, yc + 250)  # the stack's landing, at least the metal's minimum area
        rect(metal, xs - 500, yc - 500, D * 1000 - 310000, yc + 500)
    # The die's frame: IHP's seal ring, and fill inside it
    seal = m("layout_seal_ring", **top, die=[D, D])
    fill = m("layout_fill", **top)
    check(seal == f"sealring_{D}x{D}" and fill["instances"] > 1000, f"layout_seal_ring and layout_fill: {fill['instances']} fill cells placed inside the seal ring")
    drc = m("verify_drc", **top, recommended=False, density=True)
    rules = [(r["rule"], r["count"]) for r in m("verify_markers", run=drc["run"])["rules"]] if not drc.get("clean") else []
    # Seal.m ("only one sealring per chip") fires on IHP's own sealring cell alone with this deck, as on ours (XORed identical): the
    # deck's count, not the ring
    check(drc.get("density") is True and not drc.get("error") and all(r == "Seal.m" for r, _ in rules),
          f"IHP's DRC deck over the whole chip, pads, seal ring and fill: clean, density and latch-up rules included (not the recommended "
          f"ones), but for Seal.m, which IHP's own seal ring also gets from this deck {rules} {drc.get('error', '')}")
    # Its schematic: the pads, the block, and the core's nets between them
    m("view_create", **top, view="schematic", type="schematic")
    supplies = {"vdd": "vdd", "vss": "vss", "iovdd": "iovdd", "iovss": "iovss"}
    order = (("a", "sg13g2_IOPadIn", "a_core"), ("b", "sg13g2_IOPadIn", "b_core"), ("y", "sg13g2_IOPadOut4mA", "y_core"), ("vdd", "sg13g2_IOPadVdd", None),
             ("vss", "sg13g2_IOPadVss", None), ("iovdd", "sg13g2_IOPadIOVdd", None), ("iovss", "sg13g2_IOPadIOVss", None))
    for k, (name, cell, core) in enumerate(order):
        place(m, "die", "and2chip", "P_" + name, "sg13g2_io", cell, 1000 * k, 0, dict(supplies, pad=name, p2c=core, c2p=core))
        m("pin_add", **top, view="schematic", name=name, direction={"a": "input", "b": "input", "y": "output"}.get(name, "inputOutput"), x=-3000, y=100 * k)
    place(m, "die", "and2chip", "U1", "blocks", "and2", 0, 3000, {"a": "a_core", "b": "b_core", "y": "y_core", "vdd": "vdd", "vss": "vss"})
    lvs = m("verify_lvs", **top)
    circuits = {c["schematic"]: c["status"] for c in m("verify_lvs_report", run=lvs["run"], all=True)["circuits"]}
    check(lvs.get("matched") is True and lvs.get("devices_layout") == 6 and circuits.get("SG13G2_IOPADIN") == "match",
          f"IHP's LVS deck over the whole chip: matched, the block's six transistors and each pad by its pins {lvs.get('error', '')}")
    m("layout_objects_delete", **top, shapes=[b_across])
    check(m("verify_lvs", **top).get("matched") is False, "with b's wire cut, LVS no longer matches")
    m("edit_undo", library="die")
    # A schematic with the a and y pads' core nets traded (a and b would not do: the NAND's inputs are interchangeable, so LVS rightly
    # takes them either way)
    swapped = dict(top, view="swapped")
    m("view_create", **swapped, type="schematic")
    for k, (name, cell, core) in enumerate(order):
        core = {"a_core": "y_core", "y_core": "a_core"}.get(core, core)
        m("instance_add", **swapped, master_library="sg13g2_io", master_cell=cell, name="P_" + name, x=1000 * k, y=0, params={})
        for p in m("view_get", library="sg13g2_io", cell=cell, view="symbol")["pins"]:
            net = dict(supplies, pad=name, p2c=core, c2p=core).get(p["name"])
            if net:
                m("label_add", **swapped, name=net, x=1000 * k + p["at"][0], y=p["at"][1])
        m("pin_add", **swapped, name=name, direction={"a": "input", "b": "input", "y": "output"}.get(name, "inputOutput"), x=-3000, y=100 * k)
    m("instance_add", **swapped, master_library="blocks", master_cell="and2", name="U1", x=0, y=3000, params={})
    for p in m("view_get", library="blocks", cell="and2", view="symbol")["pins"]:
        m("label_add", **swapped, name={"a": "a_core", "b": "b_core", "y": "y_core"}.get(p["name"], p["name"]), x=p["at"][0], y=3000 + p["at"][1])
    check(m("verify_lvs", **top, schematic="swapped").get("matched") is False,
          "with the a and y pads' core nets traded in the schematic, LVS no longer matches: a black box keeps its pad and its core pin together")


def step_chip_test(m, work):
    print("Testing the chip built by hand: its schematic as Verilog, through the pads' and cells' models", flush=True)
    got = m("netlist_verilog", library="die", cell="and2chip")
    text = open(got["file"]).read()
    check("module and2chip" in text and "module and2" in text and "sg13g2_IOPadIn P_a" in text and any(f.endswith("sg13g2_io.v") for f in got["models"]),
          f"netlist_verilog: the top and the AND2 block as modules, the pads and standard cells by their models: {[os.path.basename(f) for f in got['models']]}")
    tb = os.path.join(work, "and2chip_tb.v")
    with open(tb, "w") as f:
        f.write("`timescale 1ns/1ps\nmodule tb;\n  reg a = 0, b = 0;\n  wire y;\n"
                "  and2chip dut (.a(a), .b(b), .y(y), .vdd(), .vss(), .iovdd(), .iovss());\n  integer k, bad = 0;\n"
                "  initial begin\n    for (k = 0; k < 4; k = k + 1) begin\n      {a, b} = k; #10;\n"
                "      if (y !== (a & b)) begin bad = bad + 1; $display(\"FAIL a=%b b=%b y=%b\", a, b, y); end\n    end\n"
                "    if (bad == 0) $display(\"PASS\");\n    $finish;\n  end\nendmodule\n")
    run = m("test_run", name="and2chip", testbench=[tb], top="tb", level="rtl", verilog=[got["file"]] + got["models"])
    check(run.get("passed") is True, f"the chip's truth table at its pads, from pad a and b through the NAND2 and the inverter to pad y {run.get('error', '')}")


def step_rv32i(m, work, flowdir):
    print("A RISC-V chip by hand: an RV32I core with an SRAM macro through the flow, a block in a pad ring, run with its firmware", flush=True)
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rv32i")
    platform = os.path.join(flowdir, "platforms/ihp-sg13g2")
    sram = "RM_IHPSG13_1P_64x64_c2_bm_bist"
    design = os.path.join(work, "rv32i")
    os.makedirs(design, exist_ok=True)
    subprocess.run([sys.executable, os.path.join(here, "firmware.py"), design], check=True)
    for f in ("pdn.tcl", "io.tcl"):  # into the work directory: ORFS's make takes no path with a space, and the repository's may have one
        shutil.copy(os.path.join(here, f), design)
    src = os.path.join(flowdir, "designs/src/riscv32i")
    rtl = [os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".v") and f not in ("top.v", "rom.v", "dmem.v")]
    rtl += [os.path.join(here, "soc_core.v"), os.path.join(design, "firmware_rom.v"), os.path.join(here, "sram_blackbox.v")]
    st = m("flow_run", design="soc_core", verilog=rtl, top="soc_core", platform="ihp-sg13g2", clock_period_ns=40.0, settings={
        "ADDITIONAL_LEFS": f"{platform}/lef/{sram}.lef", "ADDITIONAL_LIBS": f"{platform}/lib/{sram}_typ_1p20V_25C.lib",
        "ADDITIONAL_GDS": f"{platform}/gds/{sram}.gds", "ADDITIONAL_CDL": f"{platform}/cdl/{sram}.cdl", "DIE_AREA": "0 0 860 580",
        "CORE_AREA": "20 20 840 560", "MACRO_PLACE_HALO": "10 10", "PDN_TCL": os.path.join(design, "pdn.tcl"), "IO_CONSTRAINTS": os.path.join(design, "io.tcl")})
    report = m("flow_report", run=st["run"])
    check(st["state"] == "done" and report.get("clean") is True,
          f"flow_run: the core, its firmware ROM and the SRAM macro from Verilog to GDS at 40 ns, DRC clean, LVS matched (the macro by its pins), no ERC finding {report.get('not_clean')} {st.get('error', '')}")
    made = m("flow_make_macro", run=st["run"], library="rv", cell="soc_core")
    placed = {(i["library"], i["cell"]) for i in m("layout_objects_get", library="rv", cell="soc_core")["instances"]}
    check(("sg13g2_sram", sram) in placed and made["netlist"] is None and len(made["pins"]) == 14,
          f"Make Block: the core with {made['standard_cells']} standard cells and the SRAM macro placed from sg13g2_sram; no transistor netlist (the macro has none)")
    alone = m("verify_lvs", library="rv", cell="soc_core")
    check(alone.get("matched") is True and alone.get("erc_violations") == 0, f"the block alone: LVS matched against its netlist {alone.get('error', '')}")

    # The chip: 16 pads around a 1.6 x 1.4 mm die, the block in the middle
    W, H = 1600, 1400
    top = dict(library="die", cell="rv32i_chip")
    m("cell_create", **top)
    m("view_create", **top, view="layout", type="layout")
    P = lambda n, k: {"name": n, "master": k}  # noqa: E731
    ring = m("layout_pad_ring", **top, die=[W, H], south=[P("clk", "sg13g2_IOPadIn"), P("reset", "sg13g2_IOPadIn")],
             north=[P("uart_d%d" % k, "sg13g2_IOPadOut4mA") for k in range(8)],
             east=[P("uart_valid", "sg13g2_IOPadOut4mA"), P("halted", "sg13g2_IOPadOut4mA"), P("vdd", "sg13g2_IOPadVdd"), P("vss", "sg13g2_IOPadVss")],
             west=[P("iovdd", "sg13g2_IOPadIOVdd"), P("iovss", "sg13g2_IOPadIOVss")])
    pads = sorted((i["at"], i["angle"]) for i in m("layout_objects_get", **top)["instances"] if i["cell"].startswith("sg13g2_IOPad"))
    south_x = [at[0] for at, a in pads if a == 0]
    north_x = sorted(at[0] - 80000 for at, a in pads if a == 180)  # each pad's left edge
    east_y = sorted(at[1] for at, a in pads if a == 90)
    blk = m("layout_objects_get", library="rv", cell="soc_core")
    pins = {}
    for t in (s for s in blk["shapes"] if s["kind"] == "text"):
        x, y = t["points"][0]
        b = [box(s) for s in blk["shapes"] if s["datatype"] == 2 and s["layer"] == t["layer"] and box(s)[0] <= x <= box(s)[2] and box(s)[1] <= y <= box(s)[3]]
        pins.setdefault(t["text"], []).append((t["layer"], b[0]))
    O = (370000, 400000)
    m("layout_instance_add", **top, master="soc_core", masterLibrary="rv", at=list(O))

    def rect(layer, x0, y0, x1, y1):
        return m("layout_shape_add", **top, layer=layer, kind="rect", points=[[min(x0, x1), min(y0, y1)], [max(x0, x1), max(y0, y1)]])

    def via(metal, x, y, cols=1, rows=1):
        m("layout_via_add", **top, **{"from": metal}, at=[x, y], cols=cols, rows=rows)

    def pin(name, layer=None):  # its box, absolute
        b = next(b for ly, b in pins[name] if layer is None or ly == layer)
        return (O[0] + b[0], O[1] + b[1], O[0] + b[2], O[1] + b[3])
    mid = lambda a, b: (a + b) // 2 // 10 * 10  # noqa: E731

    # South: an input pad's p2c (Metal2, 39.83 to 40.12 um in) up to its own Metal3 track, across, up onto the block's bottom pin
    for name, px, track in (("clk", south_x[0], 340000), ("reset", south_x[1], 350000)):
        x, b = px + 39975, pin(name)
        X = mid(b[0], b[2])
        rect("Metal2", px + 39830, 318000, px + 40120, track + 200)
        via("Metal2", x, track)
        rect("Metal3", x, track - 200, X, track + 200)
        via("Metal2", X, track)
        rect("Metal2", X - 100, track, X + 100, b[1] + 300)
    # North: an output pad's c2p (Metal2, 38.145 to 41.855 um in; R180) down to its track, across, down onto the block's top pin. The pad
    # nearest the pins' cluster takes the highest track, so no pin's drop passes beside its pad's
    order = sorted(range(8), key=lambda k: abs(north_x[k] + 40000 - (O[0] + 830000)), reverse=True)
    for rank, k in enumerate(order):
        track, x, b = 985000 + 2000 * rank, north_x[k] + 40000, pin("uart_data[%d]" % k)
        X = mid(b[0], b[2])
        rect("Metal2", x - 200, track - 200, x + 200, H * 1000 - 318000)
        via("Metal2", x, track)
        rect("Metal3", x, track - 200, X, track + 200)
        via("Metal2", X, track)
        rect("Metal2", X - 100, b[3] - 300, X + 100, track)
    # East: an output pad's c2p (R90: a stub at the ring's inner edge) along Metal2 to the pin's height, across on Metal3 onto it
    for name, py, xc in (("uart_valid", east_y[0], 1255000), ("halted", east_y[1], 1265000)):
        y, b = py + 40000, pin(name)
        Y = mid(b[1], b[3])
        rect("Metal2", xc - 200, y - 200, W * 1000 - 318000, y + 200)
        rect("Metal2", xc - 200, min(y, Y) - 200, xc + 200, max(y, Y) + 200)
        via("Metal2", xc, Y)
        rect("Metal3", b[0] + 300, Y - 200, xc, Y + 200)
    # vdd: from the west side's innermost Metal3 rail (vdd, 300 to 318 um in), stacked to TopMetal1 in the channel beside the block (on the
    # block's ring its own stacks reach every row), then onto its outer ring (VDD, TopMetal1)
    vddb, vssb = pin("VDD", 126), pin("VSS", 126)
    y, Xv = O[1] + 290000, 350000
    rect("Metal3", 305000, y - 5000, Xv + 2000, y + 5000)
    for lower in ("Metal3", "Metal4", "Metal5"):
        via(lower, Xv, y, cols=4, rows=8)
        rect(lower, Xv - 2000, y - 5000, Xv + 2000, y + 5000)
    rect("TopMetal1", Xv - 2000, y - 5000, mid(vddb[0], vddb[2]) + 2000, y + 5000)
    # vss: the block's inner ring lies inside its VDD ring: from the ring's innermost TopMetal1 rail (vss), up to TopMetal2 over the VDD ring
    # and down onto the VSS ring (TopVia2: 0.9 um cuts 2 um apart, 0.5 um inside both metals)
    def topvia2(x0, y0, cols, rows):
        for i in range(cols):
            for j in range(rows):
                rect("TopVia2", x0 + 2000 * i, y0 + 2000 * j, x0 + 2000 * i + 900, y0 + 2000 * j + 900)
    y = O[1] + 250000
    rect("TopMetal1", 305000, y - 5000, vddb[0] - 3000, y + 5000)
    topvia2(vddb[0] - 9500, y - 3900, 3, 4)
    rect("TopMetal2", vddb[0] - 10000, y - 5000, vssb[2], y + 5000)
    topvia2(mid(vssb[0], vssb[2]) - 1450, y - 3900, 2, 4)
    # Its schematic: the pads, the block, the core's nets
    m("view_create", **top, view="schematic", type="schematic")
    sup = {"vdd": "vdd", "vss": "vss", "iovdd": "iovdd", "iovss": "iovss"}
    order = [("clk", "sg13g2_IOPadIn", "clk_core"), ("reset", "sg13g2_IOPadIn", "reset_core")] + \
            [("uart_d%d" % k, "sg13g2_IOPadOut4mA", "ud%d" % k) for k in range(8)] + \
            [("uart_valid", "sg13g2_IOPadOut4mA", "valid_core"), ("halted", "sg13g2_IOPadOut4mA", "halted_core"), ("vdd", "sg13g2_IOPadVdd", None),
             ("vss", "sg13g2_IOPadVss", None), ("iovdd", "sg13g2_IOPadIOVdd", None), ("iovss", "sg13g2_IOPadIOVss", None)]
    for k, (name, cell, core) in enumerate(order):
        place(m, "die", "rv32i_chip", "P_" + name, "sg13g2_io", cell, 1000 * k, 0, dict(sup, pad=name, p2c=core, c2p=core))
        m("pin_add", **top, view="schematic", name=name, direction="input" if name in ("clk", "reset") else "output" if core else "inputOutput", x=-3000, y=100 * k)
    nets = {"clk": "clk_core", "reset": "reset_core", "uart_valid": "valid_core", "halted": "halted_core", "VDD": "vdd", "VSS": "vss"}
    nets.update({"uart_data[%d]" % k: "ud%d" % k for k in range(8)})
    place(m, "die", "rv32i_chip", "U1", "rv", "soc_core", 0, 5000, nets)
    lvs = m("verify_lvs", **top)
    check(lvs.get("matched") is True and lvs.get("erc_violations") == 0 and lvs.get("devices_layout") == lvs.get("devices_schematic"),
          f"IHP's LVS deck over the RISC-V chip: matched, the core flat in it ({lvs.get('devices_layout')} devices a side beside its standard cells), "
          f"the pads and the SRAM by their pins, no ERC finding {lvs.get('error', '')}")
    m("layout_seal_ring", **top, die=[W, H])
    fill = m("layout_fill", **top)
    drc = m("verify_drc", **top, recommended=False, density=True)
    rules = [(r["rule"], r["count"]) for r in m("verify_markers", run=drc["run"])["rules"]] if not drc.get("clean") else []
    check(not drc.get("error") and all(r == "Seal.m" for r, _ in rules),
          f"IHP's DRC deck over the RISC-V chip with its seal ring and {fill['instances']} fill cells: clean with density and latch-up rules, "
          f"but for Seal.m (as on IHP's own seal ring) {rules} {drc.get('error', '')}")
    # Its test: the firmware at the pads, from the chip's schematic as Verilog through the pads', standard cells' and SRAM's models
    got = m("netlist_verilog", library="die", cell="rv32i_chip")
    ihp = os.path.expanduser("~/eda/ihp-verilog/RM_IHPSG13_1P_core_behavioral_bm_bist.v")
    core = ihp if os.path.isfile(ihp) else os.path.join(here, "sram_core_model.v")
    models = got["models"] + ([] if core in got["models"] else [core])
    run = m("test_run", name="rv32i_hello", testbench=[os.path.join(here, "tb_chip.v")], top="tb", level="rtl", verilog=[got["file"]] + models,
            defines=["FUNCTIONAL"], time_limit_ns=400000)
    check(run.get("passed") is True,
          f"the RISC-V chip at its pads, from its schematic, the routed core's netlist and the SRAM ({'IHP' if core == ihp else 'our'} model of its core): "
          f"Hello, world! and then halted {run.get('error', '')}")


def seal_ring_is_ihps(m, work, flowdir):
    """A seal ring for the 1050 um die of ORFS's IHP example, XORed with that example's sealring.gds.gz, layer by layer."""
    m("cell_create", library="die", cell="sr1050")
    m("view_create", library="die", cell="sr1050", view="layout", type="layout")
    m("layout_seal_ring", library="die", cell="sr1050", die=[1050, 1050])
    ours = os.path.join(work, "sr1050.gds")
    m("layout_export_gds", library="die", cell="sr1050", file=ours)
    script = os.path.join(work, "xor.py")
    with open(script, "w") as f:
        f.write("import pya\na = pya.Layout(); a.read(theirs); b = pya.Layout(); b.read(ours)\n"
                "for li in a.layer_indexes():\n"
                "    i = a.get_info(li); lb = b.find_layer(i.layer, i.datatype)\n"
                "    ra = pya.Region(a.top_cell().begin_shapes_rec(li)).merged()\n"
                "    rb = pya.Region(b.top_cell().begin_shapes_rec(lb)).merged() if lb is not None else pya.Region()\n"
                "    print(i, 'same' if (ra ^ rb).is_empty() else 'differs')\n")
    out = subprocess.run([shutil.which("klayout", path=os.path.expanduser("~/eda/bin") + os.pathsep + os.environ.get("PATH", "")), "-b", "-r", script,
                          "-rd", "theirs=" + os.path.join(flowdir, "designs/ihp-sg13g2/i2c-gpio-expander/sealring.gds.gz"), "-rd", "ours=" + ours],
                         capture_output=True, text=True).stdout.split("\n")
    differ = [line for line in out if line.endswith("differs")]
    check(len([line for line in out if line.endswith("same")]) >= 20 and differ == ["8/22 differs"],
          f"layout_seal_ring at 1050 um is IHP's own seal ring on every layer but its Metal1 filler, cut into pieces of 5 um: {differ}")


def step_macro(m, work, flowdir):
    print("A flow-built block as a hard macro: gcd", flush=True)
    st = m("flow_run", design="gcd", verilog=[os.path.join(flowdir, "designs/src/gcd/gcd.v")], top="gcd", platform="ihp-sg13g2", clock_period_ns=10.0)
    check(st["state"] == "done", f"flow_run takes gcd to GDS: {st['state']} {st.get('error', '')}")
    made = m("flow_make_macro", run=st["run"], library="gcdlib", cell="gcd")
    check(len(made["pins"]) == 56 and made["standard_cells"] > 1000, f"flow_make_macro: gcd with its 56 ports and {made['standard_cells']} standard cells")
    insts = m("layout_objects_get", library="gcdlib", cell="gcd")["instances"]
    libs = {i["library"] for i in insts if i["cell"].startswith("sg13g2_")}
    others = sorted(c for c in m("cell_list", library="gcdlib") if c != "gcd")
    check(libs == {"sg13g2_stdcell"} and all(c.startswith("gcd__") for c in others) and not any(c.startswith("sg13g2_") for c in others),
          f"it places the standard cells of sg13g2_stdcell, and its own via and fill cells are named under it: {others[:3]}")
    pins = {p["name"]: p["direction"] for p in m("view_get", library="gcdlib", cell="gcd", view="symbol")["pins"]}
    check(pins.get("req_msg[31]") == "input" and pins.get("resp_msg[0]") == "output" and pins.get("clk") == "input", "its symbol's pins take their directions from the routed Verilog")
    alone = m("verify_lvs", library="gcdlib", cell="gcd")
    check(alone.get("matched") is True and alone.get("schematic") == "cdl", f"IHP's LVS deck: the block matches its own netlist ({alone.get('devices_layout')} devices) {alone.get('error', '')}")

    # A top in a third library: the block, and a standard cell of its own
    def box(s):
        xs = [p[0] for p in s["points"]]
        ys = [p[1] for p in s["points"]]
        return min(xs), min(ys), max(xs), max(ys)
    m("library_create", library="chip", path=os.path.join(work, "chip.oalib"))
    m("layout_tech_set", library="chip", tech="sg13g2")
    top = dict(library="chip", cell="soc")
    m("cell_create", **top)
    m("view_create", **top, view="schematic", type="schematic")
    sym = m("view_get", library="gcdlib", cell="gcd", view="symbol")["pins"]
    m("instance_add", **top, view="schematic", master_library="gcdlib", master_cell="gcd", name="U1", x=0, y=0, params={})
    for k, p in enumerate(sym):
        m("label_add", **top, view="schematic", name=p["name"], x=p["at"][0], y=p["at"][1])
        m("pin_add", **top, view="schematic", name=p["name"], direction=p["direction"], x=-20000, y=100 * k)
    isym = {p["name"]: p["at"] for p in m("view_get", library="sg13g2_stdcell", cell="sg13g2_inv_1", view="symbol")["pins"]}
    m("instance_add", **top, view="schematic", master_library="sg13g2_stdcell", master_cell="sg13g2_inv_1", name="U2", x=5000, y=0, params={})
    # Its ground is the chip's (the substrate joins them); its supply is a pin of its own, as nothing wires it to the block's
    for k, (pin, net, d) in enumerate((("A", "a2", "input"), ("Y", "y2", "output"), ("VDD", "vdd2", "inputOutput"), ("VSS", "VSS", None))):
        m("label_add", **top, view="schematic", name=net, x=5000 + isym[pin][0], y=isym[pin][1])
        if d:
            m("pin_add", **top, view="schematic", name=net, direction=d, x=-25000, y=100 * k)
    m("view_create", **top, view="layout", type="layout")
    m("layout_instance_add", **top, master="gcd", masterLibrary="gcdlib", at=[0, 0])
    block = m("layout_objects_get", library="gcdlib", cell="gcd")
    metal = {8: "Metal1", 10: "Metal2", 30: "Metal3", 50: "Metal4", 67: "Metal5", 126: "TopMetal1"}
    pin_shapes = [s for s in block["shapes"] if s["datatype"] == 2]
    named = set()
    for t in (s for s in block["shapes"] if s["kind"] == "text"):
        x, y = t["points"][0]
        b = next((box(s) for s in pin_shapes if s["layer"] == t["layer"] and box(s)[0] <= x <= box(s)[2] and box(s)[1] <= y <= box(s)[3]), None)
        if b and t["text"] not in named:
            named.add(t["text"])
            m("layout_pin_add", **top, name=t["text"], metal=metal[t["layer"]], box=list(b))
    check(len(named) == len(sym), f"the top's pins over the block's: {len(named)} of {len(sym)}")
    at = (max(p[0] for s in block["shapes"] for p in s["points"]) + 20000, -30000)
    m("layout_instance_add", **top, master="sg13g2_inv_1", masterLibrary="sg13g2_stdcell", at=list(at))
    inv = m("layout_objects_get", library="sg13g2_stdcell", cell="sg13g2_inv_1")
    inv_pins = [s for s in inv["shapes"] if s["layer"] == 8 and s["datatype"] == 2]
    for t in (s for s in inv["shapes"] if s["kind"] == "text" and s["text"] in ("A", "Y", "VDD")):
        x, y = t["points"][0]
        b = next(box(s) for s in inv_pins if box(s)[0] <= x <= box(s)[2] and box(s)[1] <= y <= box(s)[3])
        m("layout_pin_add", **top, name={"A": "a2", "Y": "y2", "VDD": "vdd2"}[t["text"]], metal="Metal1", box=[b[0] + at[0], b[1] + at[1], b[2] + at[0], b[3] + at[1]])
    whole = m("verify_lvs", **top)
    circuits = {c["schematic"]: c["status"] for c in m("verify_lvs_report", run=whole["run"], all=True)["circuits"]}
    check(whole.get("matched") is True and whole.get("erc_violations") == 0 and circuits.get("GCD") == "match" and circuits.get("SOC") == "match",
          f"IHP's LVS deck: the top matches as one hierarchy across three libraries, the block's bus pins paired {whole.get('error', '')} {[k for k, v in circuits.items() if v != 'match']}")
    cdl = open(os.path.join(work, "verification", whole["run"], "schematic.cdl")).read()
    check(cdl.count(".SUBCKT sg13g2_inv_1 ") == 1, "the inverter's netlist, from the block's and from the cell library, is written once")

    # The block at transistor level, out of reset: req_rdy rises once reset falls and the clock ticks
    tb = dict(library="chip", cell="gcdtb")
    m("cell_create", **tb)
    m("view_create", **tb, view="schematic", type="schematic")
    m("instance_add", **tb, view="schematic", master_library="gcdlib", master_cell="gcd", name="U1", x=0, y=0, params={})
    for p in sym:
        net = {"VDD": "vdd", "VSS": "gnd!", "clk": "clk", "reset": "reset", "req_rdy": "req_rdy"}.get(p["name"], "gnd!" if p["direction"] == "input" else "out_" + p["name"].replace("[", "_").replace("]", ""))
        m("label_add", **tb, view="schematic", name=net, x=p["at"][0], y=p["at"][1])
    place(m, "chip", "gcdtb", "VDD", "analogLib", "vdc", -3000, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    place(m, "chip", "gcdtb", "VCLK", "analogLib", "vpulse", -4000, 0, {"PLUS": "clk", "MINUS": "gnd!"}, {"v1": "0", "v2": "1.2", "td": "5n", "tr": "50p", "tf": "50p", "pw": "5n", "per": "10n"})
    place(m, "chip", "gcdtb", "VRST", "analogLib", "vpulse", -5000, 0, {"PLUS": "reset", "MINUS": "gnd!"}, {"v1": "1.2", "v2": "0", "td": "12n", "tr": "50p", "tf": "50p", "pw": "1u", "per": "2u"})
    run = m("simulate", **tb, view="schematic", cards=[".tran 50p 18n"], timeout=1800)["run"]
    edge = m("wave_eval", run=run, expression='cross(VT(req_rdy), 0.6, 1, "rising")')
    held = [m("wave_eval", run=run, expression=f"value(VT(req_rdy), {t}n)") for t in (9.9, 14.9, 17.9)]
    print(f"    req_rdy rises {edge * 1e9:.3f} ns (the clock's first edge at 5 ns, in reset); {[round(v, 2) for v in held]} V at 9.9, 14.9 and 17.9 ns", flush=True)
    check(5e-9 < edge < 7e-9 and all(v > 1.0 for v in held),
          "simulated at transistor level through the standard cells' netlists, the first clock edge in reset sets its state: req_rdy rises, and holds after reset falls")


def step_liberate(m, work, flowdir):
    print("Liberate on TreeSPICE: standard cells characterized against IHP's Liberty", flush=True)
    libs, fn = {}, {}
    for cell in ("sg13g2_inv_1", "sg13g2_nand2_1", "sg13g2_xor2_1"):
        r = m("lib_characterize", library="sg13g2_stdcell", cell=cell)
        libs[cell], fn[cell] = r["liberty"], r["function"]
        tables = [v for a in r["arcs"] for v in a["compare"].values() if isinstance(v, dict)]
        mean = max(t["mean_relative"] for t in tables)
        caps = [abs(p["capacitance_pf"] / p["reference_pf"] - 1) for p in r["pins"].values()]
        check(len(tables) == 4 * len(r["arcs"]) and mean < LIB_MEAN and r["worst_relative"] < LIB_WORST and max(caps) < LIB_CAP,
              f"{cell}: {r['function']}, {len(r['arcs'])} arcs of {r['points']:.0f} points in {r['seconds']:.0f} s; each table within {mean:.1%} of IHP's on average, "
              f"{r['worst_relative']:.1%} at worst; pin capacitance within {max(caps):.1%}")
    check(fn["sg13g2_nand2_1"] == {"Y": "!(A*B)"} and fn["sg13g2_inv_1"] == {"Y": "!A"} and set(fn["sg13g2_xor2_1"].values()) and len(m("lib_characterize", library="sg13g2_stdcell", cell="sg13g2_xor2_1",
          slews=[0.05, 0.2], loads=[0.005, 0.05])["arcs"]) == 4, f"functions from the truth table, an XOR's two senses per input: {fn}")
    # OpenSTA reads them, and times a path through them as through IHP's own
    ihp = os.path.join(flowdir, "platforms/ihp-sg13g2/lib/sg13g2_stdcell_typ_1p20V_25C.lib")
    xor_out = next(iter(fn["sg13g2_xor2_1"]))
    with open(os.path.join(work, "path.v"), "w") as f:
        f.write("module path (a, b, y); input a, b; output y; wire n1, n2;\n  sg13g2_inv_1 u1 (.A(a), .Y(n1));\n  sg13g2_nand2_1 u2 (.A(n1), .B(b), .Y(n2));\n"
                f"  sg13g2_xor2_1 u3 (.A(n2), .B(b), .{xor_out}(y));\nendmodule\n")
    def arrival(liberties):
        lef = os.path.join(flowdir, "platforms/ihp-sg13g2/lef")
        tcl = f"read_lef {lef}/sg13g2_tech.lef\nread_lef {lef}/sg13g2_stdcell.lef\n" + "".join(f"read_liberty {l}\n" for l in liberties) + (f"read_verilog {work}/path.v\nlink_design path\nset_input_transition 0.1 [all_inputs]\n"
              "set_load 0.02 [all_outputs]\nreport_checks -unconstrained -from [get_ports a] -to [get_ports y] -digits 4\nexit\n")
        with open(os.path.join(work, "path.tcl"), "w") as f:
            f.write(tcl)
        out = subprocess.run([os.path.expanduser("~/eda/bin/openroad"), "-no_init", "-exit", os.path.join(work, "path.tcl")], capture_output=True, text=True).stdout
        times = [float(l.split()[0]) for l in out.splitlines() if "data arrival time" in l]
        return (times[0] if times else None), out
    ours, log = arrival(libs.values())
    theirs, _ = arrival([ihp])
    check(ours is not None and theirs is not None and abs(ours / theirs - 1) < LIB_PATH,
          f"OpenSTA reads the three and times a -> y through them: {ours} ns, against {theirs} ns with IHP's Liberty {'' if ours else log[-300:]}")


def step_sky130(m, flowdir):
    if not os.path.isdir(os.path.join(flowdir, "platforms/sky130hd")):
        print("SkyWater 130: SKIP (no ORFS sky130hd platform)", flush=True)
        return
    print("SkyWater 130: gcd on sky130hd", flush=True)
    st = m("flow_run", design="gcd_sky130", verilog=[os.path.join(flowdir, "designs/src/gcd/gcd.v")], top="gcd", platform="sky130hd", clock_period_ns=5.0)
    report = m("flow_report", run=st["run"])
    lvs = next(x for x in report["stages"] if x["stage"] == "lvs").get("metrics", {})
    drcLog = os.path.join(st["dir"], "logs/sky130hd/gcd_sky130/base/6_drc.log")
    check(report["clean"] and lvs.get("matched") and lvs.get("erc_violations") == 0,
          f"flow_run takes gcd to GDS on SkyWater 130: timing met, DRC clean, LVS matched ({lvs.get('devices_layout')} devices), no ERC finding {report.get('not_clean')}")
    check(os.path.isfile(drcLog) and "FEOL section" in open(drcLog).read(), "its DRC runs the deck's front-end rules too (ORFS's copy turns them off)")


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
    step_chip(m, work)
    step_chip_test(m, work)
    step_rv32i(m, work, os.path.expanduser("~/eda/orfs/flow"))
    seal_ring_is_ihps(m, work, os.path.expanduser("~/eda/orfs/flow"))
    step_macro(m, work, os.path.expanduser("~/eda/orfs/flow"))
    step_liberate(m, work, os.path.expanduser("~/eda/orfs/flow"))
    step_sky130(m, os.path.expanduser("~/eda/orfs/flow"))
    m.p.stdin.close()
    m.p.wait()
    print(("PASSED" if not G_FAIL else "FAILED") + f": {G_PASS} of {G_PASS + len(G_FAIL)} checks")
    return 1 if G_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
