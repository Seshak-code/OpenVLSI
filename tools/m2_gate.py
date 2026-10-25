#!/usr/bin/env python3
"""The M2 gate (docs/rebuild.md), driven through the MCP server alone; it grows with M2's slices.

    python3 tools/m2_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--keep DIR]

Now stb, on the M1 op-amp (tools/m1_gate.py builds it) as a follower with an iprobe in its feedback, over the five MOS
corners and three temperatures: its loop gain equals Middlebrook's two injections done by hand as ordinary AC runs, at
every frequency (exact: the same loop); and its phase margin and unity-gain frequency agree with the open loop broken
by the 1 GOhm / 1 F trick (close, not equal: that break leaves out the loading the follower's output carries).
Exit 0 when every check passes.
"""
import argparse, os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m1_gate as m1  # the MCP client, the op-amp and its test benches

check = m1.check


def stb_bench(m, cell, injection):
    """The follower with a probe in its loop (+ toward the loop's input, the op-amp's inn; - the return, its out).
    injection: None (stb's bench), "v" (the probe driven by AC 1 V) or "i" (AC 1 A into the loop's input): Middlebrook's
    two injections done by hand, as ordinary AC runs."""
    m("cell_create", library="design", cell=cell)
    m("view_create", library="design", cell=cell, view="schematic", type="schematic")
    m1.place(m, "design", cell, "I0", "design", "opamp", 0, 0, {"inp": "inp", "inn": "fb", "ibias": "nb", "out": "out", "vdd": "vdd", "vss": "gnd!"})
    m1.place(m, "design", cell, "VDD", "analogLib", "vdc", -1500, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    m1.place(m, "design", cell, "IB", "analogLib", "idc", -2000, 0, {"PLUS": "vdd", "MINUS": "nb"}, {"idc": "10u"})
    m1.place(m, "design", cell, "CL", "analogLib", "cap", 1500, 0, {"PLUS": "out", "MINUS": "gnd!"}, {"c": "1p"})
    m1.place(m, "design", cell, "VIN", "analogLib", "vdc", -2500, 0, {"PLUS": "inp", "MINUS": "gnd!"}, {"vdc": "0.6"})
    if injection == "buffered":  # the probe drives inn through an ideal unity buffer: the output carries no gate load
        m1.place(m, "design", cell, "VPRB", "analogLib", "iprobe", 2000, 0, {"PLUS": "fbx", "MINUS": "out"})
        m1.place(m, "design", cell, "EBUF", "analogLib", "vcvs", 2500, 0, {"PLUS": "fb", "MINUS": "gnd!", "NCP": "fbx", "NCN": "gnd!"}, {"gain": "1"})
        return
    if injection == "v":
        m1.place(m, "design", cell, "VPRB", "analogLib", "vdc", 2000, 0, {"PLUS": "fb", "MINUS": "out"}, {"vdc": "0", "acm": "1"})
    else:
        m1.place(m, "design", cell, "VPRB", "analogLib", "iprobe", 2000, 0, {"PLUS": "fb", "MINUS": "out"})
    if injection == "i":
        m1.place(m, "design", cell, "IINJ", "analogLib", "idc", 2500, 0, {"PLUS": "gnd!", "MINUS": "fb"}, {"idc": "0", "acm": "1"})


def broken_bench(m):
    """The loop broken by hand where the follower closes it, at inn: DC held by 1 GOhm from out and 1 F, the 1 F driven
    by AC 1 so inn carries the test signal; inp at AC ground. T = -V(out)/V(inn). (tb_ol drives inp instead: that is
    the non-inverting gain, another path, through no mirror.)"""
    m("cell_create", library="design", cell="tb_broken")
    m("view_create", library="design", cell="tb_broken", view="schematic", type="schematic")
    m1.place(m, "design", "tb_broken", "I0", "design", "opamp", 0, 0, {"inp": "inp", "inn": "inn", "ibias": "nb", "out": "out", "vdd": "vdd", "vss": "gnd!"})
    m1.place(m, "design", "tb_broken", "VDD", "analogLib", "vdc", -1500, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    m1.place(m, "design", "tb_broken", "IB", "analogLib", "idc", -2000, 0, {"PLUS": "vdd", "MINUS": "nb"}, {"idc": "10u"})
    m1.place(m, "design", "tb_broken", "CL", "analogLib", "cap", 1500, 0, {"PLUS": "out", "MINUS": "gnd!"}, {"c": "1p"})
    m1.place(m, "design", "tb_broken", "VIN", "analogLib", "vdc", -2500, 0, {"PLUS": "inp", "MINUS": "gnd!"}, {"vdc": "0.6"})
    m1.place(m, "design", "tb_broken", "RF", "analogLib", "res", 2000, 0, {"PLUS": "out", "MINUS": "inn"}, {"r": "1G"})
    m1.place(m, "design", "tb_broken", "CF", "analogLib", "cap", 2500, 0, {"PLUS": "inn", "MINUS": "acin"}, {"c": "1"})
    m1.place(m, "design", "tb_broken", "VAC", "analogLib", "vdc", 3000, 0, {"PLUS": "acin", "MINUS": "gnd!"}, {"vdc": "0", "acm": "1"})


def wave(m, run, expression):
    """A complex waveform from wave_eval: (x, [complex])"""
    w = m("wave_eval", run=run, expression=expression)
    return w["x"], [complex(r, i) for r, i in zip(w["re"], w["im"])] if "re" in w else [complex(y, 0) for y in w["y"]]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mcp", required=True)
    ap.add_argument("--pdk", required=True, help="the ihp-sg13g2 directory")
    ap.add_argument("--osdi", required=True, help="psp103.osdi")
    ap.add_argument("--keep", help="build the workspace here (kept) instead of a temporary directory")
    a = ap.parse_args()
    ws = a.keep or tempfile.mkdtemp(prefix="m2 gate ")
    os.makedirs(ws, exist_ok=True)
    m = m1.Mcp(os.path.abspath(a.mcp), os.path.join(ws, "openvlsi.libs"))
    print(f"workspace: {ws}")

    print("1. the op-amp, its open-loop bench and a follower with a probe in its loop")
    pdk = m("pdk_install", pdk="ihp-sg13g2", root=a.pdk, osdi=a.osdi)
    m1.build(m)
    for cell, inj in (("tb_stb", None), ("tb_injv", "v"), ("tb_inji", "i"), ("tb_stbbuf", "buffered")):
        stb_bench(m, cell, inj)
    broken_bench(m)
    check(all(m("schematic_check", library="design", cell=c, view="schematic")["ok"] for c in ("tb_stb", "tb_injv", "tb_inji", "tb_stbbuf", "tb_broken")),
          "the stb bench and the two injection benches check clean")

    print("2. stb against the loop broken by hand, over five corners and three temperatures")
    lv = pdk["models"]["mos_lv"]
    m("cell_create", library="design", cell="stbbench")
    m("view_create", library="design", cell="stbbench", view="ade", type="maestro")
    view = dict(library="design", cell="stbbench", view="ade")
    sweep = {"sweep": "dec", "points": "40", "start": "1", "stop": "10G"}
    m("ade_setup_set", setup={
        "tests": [{"name": "ol", "design": "design/tb_broken/schematic", "analyses": [{"type": "op"}, dict({"type": "ac"}, **sweep)]},
                  {"name": "stb", "design": "design/tb_stb/schematic", "analyses": [dict({"type": "stb", "probe": "VPRB"}, **sweep)]},
                  {"name": "injv", "design": "design/tb_injv/schematic", "analyses": [dict({"type": "ac"}, **sweep)]},
                  {"name": "inji", "design": "design/tb_inji/schematic", "analyses": [dict({"type": "ac"}, **sweep)]},
                  {"name": "buf", "design": "design/tb_stbbuf/schematic", "analyses": [dict({"type": "stb", "probe": "VPRB"}, **sweep)]}],
        "temperature": "-40 27 125",
        "corners": [{"name": c, "models": [{"file": lv, "section": "mos_" + c}]} for c in ("ss", "ff", "sf", "fs")],
        "outputs": [{"name": "pm", "test": "ol", "expression": "phaseMargin(-VF(out) / VF(inn))"},
                    {"name": "ugf", "test": "ol", "expression": "ugf(-VF(out) / VF(inn))"},
                    {"name": "pm_stb", "test": "stb", "expression": "phaseMargin(STB(loopgain))"},
                    {"name": "ugf_stb", "test": "stb", "expression": "ugf(STB(loopgain))"},
                    {"name": "pm_buf", "test": "buf", "expression": "phaseMargin(STB(loopgain))"},
                    {"name": "ugf_buf", "test": "buf", "expression": "ugf(STB(loopgain))"}]}, **view)
    h = m("ade_run", **view)
    pts = h["points"]
    check(h["status"] == "done" and len(pts) == 5 * 15 and all(p["status"] == "done" for p in pts), f"{len(pts)} points ran")
    by, dirs = {}, {}
    for p in pts:
        by.setdefault((p["corner"], p["temperature"]), {})[p["test"]] = p["outputs"]
        dirs.setdefault((p["corner"], p["temperature"]), {})[p["test"]] = p["dir"]

    # stb against Middlebrook's injections done by hand: Tv = -V(out)/V(fb) with the probe driven, ip = I(VPRB) with 1 A
    # into fb, T = (Tv ip - (1 - ip)) / ((Tv + 2)(1 - ip) + ip), at every frequency of every point
    worst_t = (0, None)
    for key, d in dirs.items():
        f, T = wave(m, d["stb"], "STB(loopgain)")
        _, vout = wave(m, d["injv"], "VF(out)")
        _, vfb = wave(m, d["injv"], "VF(fb)")
        _, ip = wave(m, d["inji"], "IF(VPRB)")
        for k in range(len(f)):
            tv = -vout[k] / vfb[k]
            ref = (tv * ip[k] - (1 - ip[k])) / ((tv + 2) * (1 - ip[k]) + ip[k])
            err = abs(T[k] - ref) / abs(ref)
            if err >= worst_t[0]: worst_t = (err, key, f[k])
    check(worst_t[0] <= 1e-6, f"stb's loop gain equals the two injections done by hand, at every frequency of every point "
                              f"(worst {worst_t[0]:.2e}, {worst_t[1][0]} {worst_t[1][1]} C, {worst_t[2]:.4g} Hz)")
    worst_pm, worst_ugf = (0, None), (0, None)
    for key, t in by.items():
        dpm = abs(t["stb"]["pm_stb"]["value"] - t["ol"]["pm"]["value"])
        dugf = abs(t["stb"]["ugf_stb"]["value"] / t["ol"]["ugf"]["value"] - 1)
        if dpm >= worst_pm[0]: worst_pm = (dpm, key, t["stb"]["pm_stb"]["value"], t["ol"]["pm"]["value"])
        if dugf >= worst_ugf[0]: worst_ugf = (dugf, key, t["stb"]["ugf_stb"]["value"], t["ol"]["ugf"]["value"])
    # close, not equal: in the follower the output also drives the input pair's gate (a few fF on 1 pF), which stb
    # counts and the 1 GOhm / 1 F break does not (the buffered check below removes it)
    check(worst_pm[0] <= 0.5, f"stb's phase margin matches the broken loop's at every point (worst {worst_pm[2]:.3f} vs {worst_pm[3]:.3f} deg, "
                              f"{worst_pm[1][0]} {worst_pm[1][1]} C)")
    check(worst_ugf[0] <= 0.02, f"stb's unity-gain frequency matches the broken loop's (worst {worst_ugf[2]:.6g} vs {worst_ugf[3]:.6g} Hz, "
                                f"{100 * worst_ugf[0]:.2f}%, {worst_ugf[1][0]} {worst_ugf[1][1]} C)")
    # the explanation, shown: with the gate load removed by an ideal buffer, stb's loop is the broken loop's
    worst_b = (0, None)
    for key, t in by.items():
        d = max(abs(t["buf"]["pm_buf"]["value"] - t["ol"]["pm"]["value"]), 100 * abs(t["buf"]["ugf_buf"]["value"] / t["ol"]["ugf"]["value"] - 1))
        if d >= worst_b[0]: worst_b = (d, key, t["buf"]["pm_buf"]["value"], t["ol"]["pm"]["value"])
    check(worst_b[0] <= 0.02, f"buffered so the output carries no gate load, stb matches the broken loop to {worst_b[0]:.4f} deg / % "
                              f"(worst {worst_b[2]:.4f} vs {worst_b[3]:.4f} deg, {worst_b[1][0]} {worst_b[1][1]} C): the difference above is loading")
    m.close()
    failed = [w for ok, w in m1.CHECKS if not ok]
    print(f"\n{'PASSED' if not failed else 'FAILED'}: {len(m1.CHECKS) - len(failed)} of {len(m1.CHECKS)} checks")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
