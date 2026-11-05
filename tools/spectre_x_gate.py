#!/usr/bin/env python3
"""Spectre X's role, from ADE (docs/rebuild.md, M2 slice 7), through openvlsi-mcp alone: the same large test run as it was, on more threads, and as
an array on TreeSPICE's quotient circuit. Their results must agree; what each costs is reported as measured, not promised.

    python3 tools/spectre_x_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--levels 3] [--keep DIR]

The design: identical SG13G2 inverters through schematic hierarchy (16 per level), every output on one loaded net: the best case for an
array's quotient, and no measure of a design with fewer identical parts.
"""
import argparse
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import openvlsi  # noqa: E402

FAILS = []


def check(ok, what):
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}", flush=True)
    if not ok:
        FAILS.append(what)


def place(ov, cell, name, master_lib, master, x, y, nets, params=None):
    ov.instance_add(library="d", cell=cell, view="schematic", master_library=master_lib, master_cell=master, name=name, x=x, y=y, params=params or {})
    for p in ov.view_get(library=master_lib, cell=master, view="symbol")["pins"]:
        if p["name"] in nets:
            ov.label_add(library="d", cell=cell, view="schematic", name=nets[p["name"]], x=x + p["at"][0], y=y + p["at"][1])


def new_cell(ov, cell):
    ov.cell_create(library="d", cell=cell)
    ov.view_create(library="d", cell=cell, view="schematic", type="schematic")


def build(ov, levels, per):
    ov.library_create(library="d", path="d.oalib")
    new_cell(ov, "inv")
    for k, (pin, direction) in enumerate([("in", "input"), ("out", "output"), ("vdd", "inputOutput"), ("vss", "inputOutput")]):
        ov.pin_add(library="d", cell="inv", view="schematic", name=pin, direction=direction, x=-2000, y=1000 - 300 * k)
    place(ov, "inv", "MN", "sg13g2", "sg13_lv_nmos", 0, 0, {"D": "out", "G": "in", "S": "vss", "B": "vss"}, {"w": "1u", "l": "0.13u"})
    place(ov, "inv", "MP", "sg13g2", "sg13_lv_pmos", 1000, 0, {"D": "out", "G": "in", "S": "vdd", "B": "vdd"}, {"w": "2u", "l": "0.13u"})
    ov.symbol_generate(library="d", cell="inv")
    below = "inv"
    pins = {"in": "in", "out": "out", "vdd": "vdd", "vss": "vss"}
    for level in range(levels):
        cell = f"bank{level}"
        new_cell(ov, cell)
        for k, (pin, direction) in enumerate([("in", "input"), ("out", "output"), ("vdd", "inputOutput"), ("vss", "inputOutput")]):
            ov.pin_add(library="d", cell=cell, view="schematic", name=pin, direction=direction, x=-2000, y=1000 - 300 * k)
        for k in range(per):
            place(ov, cell, f"I{k}", "d", below, 3000 * (k % 4), -3000 * (k // 4), pins)
        ov.symbol_generate(library="d", cell=cell)
        below = cell
    new_cell(ov, "tb")
    place(ov, "tb", "I0", "d", below, 0, 0, {"in": "in", "out": "out", "vdd": "vdd", "vss": "gnd!"})
    place(ov, "tb", "VDD", "analogLib", "vdc", -3000, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    place(ov, "tb", "VIN", "analogLib", "vpulse", -4000, 0, {"PLUS": "in", "MINUS": "gnd!"}, {"v1": "0", "v2": "1.2", "td": "0.1n", "tr": "50p", "tf": "50p", "pw": "1n", "per": "2n"})
    place(ov, "tb", "CL", "analogLib", "cap", 3000, 0, {"PLUS": "out", "MINUS": "gnd!"}, {"c": f"{5 * per ** levels}f"})
    ov.view_create(library="d", cell="tb", view="ade", type="maestro")


def setup(models, acceleration=None):
    test = {"name": "tr", "design": "d/tb/schematic", "analyses": [{"type": "tran", "step": "10p", "stop": "3n"}], "models": models}
    if acceleration:
        test["acceleration"] = acceleration
    return {"tests": [test],
            "outputs": [{"test": "tr", "name": "low", "expression": "value(VT(out), 0.9n)"}, {"test": "tr", "name": "high", "expression": "value(VT(out), 2.0n)"},
                        {"test": "tr", "name": "tfall", "expression": "cross(VT(out), 0.6, 1, \"falling\")"}]}


def run(ov, models, acceleration):
    ov.ade_setup_set(library="d", cell="tb", view="ade", setup=setup(models, acceleration))
    history = ov.ade_run(library="d", cell="tb", view="ade")
    point = history["points"][0]
    if point["status"] != "done":
        raise SystemExit(f"{acceleration}: {point.get('error')}")
    return point["seconds"], {k: v.get("value") for k, v in point["outputs"].items()}, point


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mcp", required=True)
    p.add_argument("--pdk", required=True)
    p.add_argument("--osdi", required=True)
    p.add_argument("--levels", type=int, default=3)
    p.add_argument("--per", type=int, default=16)
    p.add_argument("--threads", type=int, default=max(2, (os.cpu_count() or 4) - 2))
    p.add_argument("--keep")
    a = p.parse_args()
    a.mcp = os.path.abspath(a.mcp)
    work = os.path.abspath(a.keep) if a.keep else tempfile.mkdtemp(prefix="sxgate")
    if a.keep:
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    ov = openvlsi.connect(os.path.join(work, "openvlsi.libs"), mcp=a.mcp)
    ov.pdk_install(pdk="ihp-sg13g2", root=os.path.abspath(a.pdk), osdi=os.path.abspath(a.osdi))
    build(ov, a.levels, a.per)
    transistors = 2 * a.per ** a.levels
    print(f"{transistors} transistors through {a.levels} levels of hierarchy", flush=True)
    models = []
    array_s, array, point = run(ov, models, {"array": "1"})
    print(f"  array, quotient circuit: {array_s:.2f} s {array}", flush=True)
    one_s, one, _ = run(ov, models, {"threads": "1"})
    print(f"  flat, 1 thread: {one_s:.2f} s {one}", flush=True)
    many_s, many, _ = run(ov, models, {"threads": str(a.threads)})
    print(f"  flat, {a.threads} threads: {many_s:.2f} s {many}", flush=True)

    for name in one:
        check(abs(many[name] - one[name]) <= 1e-9 * max(1, abs(one[name])) + 1e-13, f"{name}: threads change nothing: {one[name]:.6g}, {many[name]:.6g}")
        tol = 1e-3 if name != "tfall" else 1e-12
        check(abs(array[name] - one[name]) <= tol, f"{name}: the array's quotient agrees with the flat run: {array[name]:.6g}, {one[name]:.6g}")
    check(one["low"] < 0.1 and one["high"] > 1.1, f"it really switches: {one['low']:.3f} V and {one['high']:.3f} V")
    check(many_s < one_s, f"{a.threads} threads are faster: {one_s / many_s:.2f} x")
    check(array_s < one_s, f"the array is faster: {one_s / array_s:.1f} x")
    print(f"Measured on {transistors} transistors: {a.threads} threads {one_s / many_s:.2f} x, array {one_s / array_s:.1f} x")
    state = ov.ade_setup_get(library="d", cell="tb", view="ade")
    check(state["tests"][0]["acceleration"]["threads"] == str(a.threads), "the setup keeps its acceleration")
    try:
        ov.ade_setup_set(library="d", cell="tb", view="ade", setup=setup(models, {"threads": "0"}))
        check(False, "zero threads are refused")
    except openvlsi.OpenVlsiError as e:
        check("threads" in str(e), "zero threads are refused")
    ov.close()
    print(("PASSED" if not FAILS else f"FAILED ({len(FAILS)})"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
