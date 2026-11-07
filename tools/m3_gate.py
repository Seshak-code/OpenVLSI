#!/usr/bin/env python3
"""The M3 gate (docs/rebuild.md): layout through openvlsi-mcp alone, against the PDK's own decks.

    python3 tools/m3_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--platform ~/eda/orfs/flow/platforms/ihp-sg13g2] [--klayout ~/eda/bin/klayout] [--keep DIR]

Steps:
1. The transistor and tie generators over a sweep of sizes: clean under IHP's maximal DRC deck.
2. An inverter: schematic, layout generated from it, routed by commands; connectivity reports what remains until nothing does;
   the routed layout is clean under the DRC deck and matches its schematic under IHP's LVS deck.
3. A deliberate short and an open are seen: by the connectivity check and by LVS.
Density rules are off: they judge a whole chip with its fill, not one cell. Latch-up rules are on for the inverter.
"""
import argparse
import json
import os
import re
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
        self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "m3_gate", "version": "1"}})
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


def run_drc(klayout, platform, gds, work, latch_up=True):
    report = os.path.join(work, os.path.basename(gds) + ".lyrdb")
    out = subprocess.run([klayout, "-b", "-rd", "in_gds=" + gds, "-rd", "report_file=" + report, "-rd", "densityRules=false", "-rd", "latchUpRules=" + ("true" if latch_up else "false"),
                          "-r", os.path.join(platform, "drc", "sg13g2_maximal.lydrc")], capture_output=True, text=True)
    log = out.stdout + out.stderr
    if "Number of DRC errors" not in log:
        raise RuntimeError("the DRC deck did not finish:\n" + log[-2000:])
    rules = {}
    for item in re.findall(r"<category>'?([^<']+)'?</category>", open(report).read()):
        rules[item] = rules.get(item, 0) + 1
    total = int(re.search(r"Number of DRC errors:\s*(\d+)", log).group(1))
    return total, rules, report


def run_lvs(klayout, platform, gds, cdl, top, work):
    deck_dir = os.path.join(platform, "lvs")
    deck = os.path.join(work, "lvs.lvs")
    text = open(os.path.join(deck_dir, "sg13g2.lvs")).read()
    text = text.replace("# %include rule_decks/", "# %include " + os.path.join(deck_dir, "rule_decks") + "/")
    open(deck, "w").write(text)
    out = subprocess.run([klayout, "-b", "-rd", "input=" + gds, "-rd", "schematic=" + cdl, "-rd", "topcell=" + top, "-rd", "report=" + os.path.join(work, top + ".lvsdb"),
                          "-rd", "target_netlist=" + os.path.join(work, top + "_extracted.cir"), "-rd", "run_mode=deep", "-r", deck], capture_output=True, text=True)
    log = out.stdout + out.stderr
    if "Netlists" not in log:
        raise RuntimeError("the LVS deck did not finish:\n" + log[-2000:])
    return "Netlists match" in log, log


SWEEP = [
    ("nmos", 300, 130, 1), ("nmos", 740, 130, 1), ("nmos", 1000, 130, 1), ("nmos", 1000, 500, 1), ("nmos", 5000, 1000, 1), ("nmos", 2000, 130, 2),
    ("nmos", 1320, 130, 3), ("nmos", 4000, 200, 4), ("nmos", 3000, 300, 5), ("pmos", 300, 130, 1), ("pmos", 1120, 130, 1), ("pmos", 2000, 130, 1),
    ("pmos", 2000, 130, 2), ("pmos", 6000, 350, 4), ("pmos", 3000, 300, 5),
]


def step_sweep(m, a, work):
    print("Generators against the DRC deck", flush=True)
    m("library_create", library="sweep", path=os.path.join(work, "sweep.oalib"))
    m("layout_tech_set", library="sweep", tech="sg13g2")
    m("cell_create", library="sweep", cell="top")
    m("view_create", library="sweep", cell="top", view="layout", type="layout")
    x = 0
    for t in ("ptap", "ntap"):
        m("layout_instance_add", library="sweep", cell="top", master=m("layout_pcell_tap", library="sweep", type=t), at=[x, 0])
        x += 3000
    y = 0
    for k, (t, w, l, ng) in enumerate(SWEEP):
        name = m("layout_pcell_mos", library="sweep", type=t, w=w, l=l, ng=ng)
        m("layout_instance_add", library="sweep", cell="top", master=name, at=[(k % 5) * 12000, 3000 + (k // 5) * 12000])
    gds = os.path.join(work, "sweep.gds")
    m("layout_export_gds", library="sweep", cell="top", file=gds)
    total, rules, report = run_drc(a.klayout, a.platform, gds, work, latch_up=False)  # ties are placement's, in the inverter below
    check(total == 0, f"{len(SWEEP)} transistor sizes and both ties: {total} DRC errors" + (f" ({rules}; {report})" if total else ""))
    check("below the process" in m.refused("layout_pcell_mos", library="sweep", type="nmos", w=1000, l=100), "a gate below 130 nm is refused")
    check("narrower" in m.refused("layout_pcell_mos", library="sweep", type="nmos", w=200, l=130), "a finger narrower than its contact is refused")
    check("grid" in m.refused("layout_pcell_mos", library="sweep", type="nmos", w=1003, l=130), "an off-grid width is refused")


def connectivity(m, cell="inv", lib="design"):
    return m("layout_connectivity", library=lib, cell=cell)


def schematic(m):
    m("library_create", library="design", path="design.oalib")
    m("layout_tech_set", library="design", tech="sg13g2")
    m("cell_create", library="design", cell="inv")
    m("view_create", library="design", cell="inv", view="schematic", type="schematic")
    masters = {"MN": ("sg13_lv_nmos", 0, 0, {"w": "1u", "l": "0.13u"}), "MP": ("sg13_lv_pmos", 0, 500, {"w": "2u", "l": "0.13u"})}
    nets = {"MN": {"D": "out", "G": "in", "S": "vss", "B": "vss"}, "MP": {"D": "out", "G": "in", "S": "vdd", "B": "vdd"}}
    pins = {"D": (0, 100), "G": (-100, 0), "S": (0, -100), "B": (100, 0)}
    for name, (master, x, y, params) in masters.items():
        m("instance_add", library="design", cell="inv", view="schematic", master_library="sg13g2", master_cell=master, name=name, x=x, y=y, params=params)
        for pin, net in nets[name].items():
            m("label_add", library="design", cell="inv", view="schematic", name=net, x=x + pins[pin][0], y=y + pins[pin][1])
    for k, (pin, d) in enumerate([("in", "input"), ("out", "output"), ("vdd", "inputOutput"), ("vss", "inputOutput")]):
        m("pin_add", library="design", cell="inv", view="schematic", name=pin, direction=d, x=-2000, y=1000 - 300 * k)


def bounds(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def route(m):
    """The inverter by hand, as a person would at the keyboard: gates on Metal1, the rest on Metal2 over vias."""
    lib = dict(library="design", cell="inv")
    objects = m("layout_objects_get", **lib)
    inst = {i["cell"]: i for i in objects["instances"]}
    term = lambda master: json.loads(m("prop_get", library="design", cell=master, view="layout", key="terminals")) if isinstance(m("prop_get", library="design", cell=master, view="layout", key="terminals"), str) else m("prop_get", library="design", cell=master, view="layout", key="terminals")
    nmos = next(i for c, i in inst.items() if c.startswith("nmos"))
    pmos = next(i for c, i in inst.items() if c.startswith("pmos"))
    ptap, ntap = inst["ptap"], inst["ntap"]

    def rect(i, name):
        r = term(i["cell"])[name][0]
        return r[1] + i["at"][0], r[2] + i["at"][1], r[3] + i["at"][0], r[4] + i["at"][1]

    def metal(layer, x0, y0, x1, y1):
        return m("layout_shape_add", **lib, layer=layer, kind="rect", points=[[x0, y0], [x1, y1]])

    def via(x, y):
        m("layout_via_add", **lib, **{"from": "Metal1"}, at=[x, y])

    def centre(r):
        return (r[0] + r[2]) // 2 // 5 * 5, (r[1] + r[3]) // 2 // 5 * 5

    # Each pin next to where its net arrives: move its three shapes together
    def move_pin(name, x, y):
        shapes = objects["shapes"]
        text = next(s for s in shapes if s["kind"] == "text" and s["text"] == name)
        tx, ty = text["points"][0]
        ids = [s["id"] for s in shapes if s["kind"] == "polygon" and bounds(s["points"])[0] <= tx <= bounds(s["points"])[2] and bounds(s["points"])[1] <= ty <= bounds(s["points"])[3]] + [text["id"]]
        m("layout_objects_move", **lib, shapes=ids, dx=x - tx, dy=y - ty)

    gn, gp, dn, dp, sn, sp = rect(nmos, "G"), rect(pmos, "G"), rect(nmos, "D"), rect(pmos, "D"), rect(nmos, "S"), rect(pmos, "S")
    tn, tp = rect(ptap, "B"), rect(ntap, "B")

    # in: both gates, left along their own rows on Metal1, then up a Metal1 column
    column = -600
    metal("Metal1", column - 130, gn[1], gn[0] + 10, gn[3])
    metal("Metal1", column - 130, gp[1], gp[0] + 10, gp[3])
    metal("Metal1", column - 130, gn[1], column + 130, gp[3])
    move_pin("in", column, centre(gn)[1])

    # out: the two drains on Metal2, then left to the pin on Metal2
    xd = centre(dn)[0]
    yn, yp = dn[3] - 145, dp[1] + 145
    via(xd, yn)
    via(xd, yp)
    metal("Metal2", xd - 100, yn - 145, xd + 100, yp + 145)
    ymid = (gn[3] + gp[1]) // 2 // 5 * 5
    metal("Metal2", -1300, ymid - 100, xd + 100, ymid + 100)
    move_pin("out", -1300, ymid)
    via(-1300, ymid)

    # vss and vdd: rails on Metal2 from source to tie, out to the left, on the far side of the drains' stub
    rail = sn[1] + 145
    via(centre(sn)[0], rail)
    metal("Metal2", -1300, rail - 100, tn[2] + 40, rail + 100)
    metal("Metal1", tn[0], rail - 130, tn[2], tn[1] + 40)  # the tie's Metal1 down to the rail
    via(centre(tn)[0], rail)
    move_pin("vss", -1300, rail)
    via(-1300, rail)

    rail = sp[3] - 145
    via(centre(sp)[0], rail)
    metal("Metal2", -1300, rail - 100, tp[2] + 40, rail + 100)
    metal("Metal1", tp[0], tp[3] - 40, tp[2], rail + 130)
    via(centre(tp)[0], rail)
    move_pin("vdd", -1300, rail)
    via(-1300, rail)


CDL = """.SUBCKT inv in out vdd vss
MN out in vss vss sg13_lv_nmos w=1u l=130n ng=1
MP out in vdd vdd sg13_lv_pmos w=2u l=130n ng=1
.ENDS
"""


def step_inverter(m, a, work):
    print("An inverter, from schematic to a layout that passes DRC and LVS", flush=True)
    schematic(m)
    gen = m("layout_generate", library="design", cell="inv")
    check(gen["devices"] == 2 and gen["pins"] == 4 and gen["instances"] == 4, "layout_generate: two devices, two ties, four pins")
    before = connectivity(m)
    open_nets = sorted(n["net"] for n in before["nets"] if n["groups"] > 1)
    check(open_nets == ["in", "out", "vdd", "vss"] and not before["shorts"], "before routing, every net is flight lines: " + ", ".join(open_nets))
    route(m)
    after = connectivity(m)
    still = [(n["net"], n["groups"]) for n in after["nets"] if n["groups"] > 1]
    check(after["clean"], f"routed: connectivity is clean (open {still}, shorts {after['shorts']})")

    gds = os.path.join(work, "inv.gds")
    cdl = os.path.join(work, "inv.cdl")
    open(cdl, "w").write(CDL)
    m("layout_export_gds", library="design", cell="inv", file=gds)
    total, rules, report = run_drc(a.klayout, a.platform, gds, work)
    check(total == 0, f"IHP's DRC deck: {total} errors" + (f" ({rules}; {report})" if total else ""))
    matched, log = run_lvs(a.klayout, a.platform, gds, cdl, "inv", work)
    check(matched, "IHP's LVS deck: the layout matches its schematic" + ("" if matched else "\n" + log[-1500:]))

    print("The same through the Verify stage's commands", flush=True)
    drc = m("verify_drc", library="design", cell="inv")
    check(drc.get("clean") is True and drc["violations"] == 0, f"verify_drc: clean ({drc.get('error', '')})")
    lvs = m("verify_lvs", library="design", cell="inv")
    check(lvs.get("matched") is True and lvs["devices_layout"] == lvs["devices_schematic"] == 2, f"verify_lvs: matched, two devices a side ({lvs.get('error', '')}, {lvs.get('devices_layout')}, {lvs.get('devices_schematic')})")
    # A Metal1 rectangle 100 nm from the left of the in pin's column, where the spacing rule says 180
    objects = m("layout_objects_get", library="design", cell="inv")
    text = next(s for s in objects["shapes"] if s["kind"] == "text" and s["text"] == "in")
    tx, ty = text["points"][0]
    m("layout_shape_add", library="design", cell="inv", layer="Metal1", kind="rect", points=[[tx - 130 - 100 - 400, ty - 200], [tx - 130 - 100, ty + 200]])
    bad = m("verify_drc", library="design", cell="inv")
    check(not bad["clean"] and bad["violations"] > 0, f"a Metal1 rectangle 100 nm from a wire is found: {bad.get('violations')} violations")
    markers = m("verify_markers", run=bad["run"])
    rules = [r["rule"] for r in markers["rules"]]
    check("M1.b" in rules, f"as M1.b, the spacing rule: {rules}")
    m1b = next(r for r in markers["rules"] if r["rule"] == "M1.b")
    xs = [p[0] for mk in m1b["markers"] for p in mk["points"]]
    ys = [p[1] for mk in m1b["markers"] for p in mk["points"]]
    gap_x = (tx - 130 - 100) / 1000
    check(any(abs(x - gap_x) < 0.15 for x in xs) and min(ys) <= (ty + 200) / 1000 and max(ys) >= (ty - 200) / 1000, f"and its marker is at the gap, x near {gap_x:.2f} um")
    worse = m("verify_lvs", library="design", cell="inv")
    check(worse.get("matched") is True, "that rectangle joins nothing, so LVS still matches")
    m("edit_undo", library="design")
    check(m("verify_drc", library="design", cell="inv")["clean"], "undo makes it clean again")

    print("A short and an open are seen", flush=True)
    objects = m("layout_objects_get", library="design", cell="inv")
    outs = [s for s in objects["shapes"] if s["layer"] == 10 and s["datatype"] == 0]
    longest = max(outs, key=lambda s: bounds(s["points"])[2] - bounds(s["points"])[0] + bounds(s["points"])[3] - bounds(s["points"])[1])
    m("layout_objects_delete", library="design", cell="inv", shapes=[longest["id"]])
    broken = connectivity(m)
    opened = [n["net"] for n in broken["nets"] if n["groups"] > 1]
    check(len(opened) == 1, f"deleting a rail opens one net: {opened}")
    m("layout_export_gds", library="design", cell="inv", file=gds)
    matched, _ = run_lvs(a.klayout, a.platform, gds, cdl, "inv", work)
    check(not matched, "and LVS no longer matches")
    m("edit_undo", library="design")
    check(connectivity(m)["clean"], "undo restores it")

    box = bounds(longest["points"])
    m("layout_shape_add", library="design", cell="inv", layer="Metal2", kind="rect", points=[[box[0], box[1] - 6000], [box[2], box[3] + 6000]])
    check(bool(connectivity(m)["shorts"]), "a Metal2 rectangle across the rails is reported as a short: " + str(connectivity(m)["shorts"]))
    m("edit_undo", library="design")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mcp", required=True)
    p.add_argument("--pdk", required=True)
    p.add_argument("--osdi", required=True)
    p.add_argument("--platform", default=os.path.expanduser("~/eda/orfs/flow/platforms/ihp-sg13g2"))
    p.add_argument("--klayout", default=os.path.expanduser("~/eda/bin/klayout"))
    p.add_argument("--keep")
    a = p.parse_args()
    a.mcp = os.path.abspath(a.mcp)
    if not os.path.exists(os.path.join(a.platform, "drc", "sg13g2_maximal.lydrc")) or not os.path.exists(a.klayout):
        print("SKIP: no IHP platform or KLayout (scripts/setup_eda_tools.sh)")
        return 0
    work = os.path.abspath(a.keep) if a.keep else tempfile.mkdtemp(prefix="m3 gate ")
    if a.keep:
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    m = Mcp(a.mcp, os.path.join(work, "openvlsi.libs"))
    m("pdk_install", pdk="ihp-sg13g2", root=os.path.abspath(a.pdk), osdi=os.path.abspath(a.osdi))
    step_sweep(m, a, work)
    step_inverter(m, a, work)
    m.p.stdin.close()
    m.p.wait()
    print(("PASSED" if not G_FAIL else "FAILED") + f": {G_PASS} of {G_PASS + len(G_FAIL)} checks")
    return 1 if G_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
