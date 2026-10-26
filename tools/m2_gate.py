#!/usr/bin/env python3
"""The M2 gate (docs/rebuild.md), driven through the MCP server alone; it grows with M2's slices.

    python3 tools/m2_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--keep DIR]

    python3 tools/m2_gate.py ... [--ngspice ngspice]

stb, on the M1 op-amp (tools/m1_gate.py builds it) as a follower with an iprobe in its feedback, over the five MOS
corners and three temperatures: its loop gain equals Middlebrook's two injections done by hand as ordinary AC runs, at
every frequency (exact: the same loop); and its phase margin and unity-gain frequency agree with the open loop broken
by the 1 GOhm / 1 F trick (close, not equal: that break leaves out the loading the follower's output carries).

PSS, against long transients' last periods on TreeSPICE and on ngspice (ngspice's own .pss is no reference: it treats
every circuit as an oscillator and, on an LC oscillator, reports convergence 20 % off its frequency): the follower
driven by a large 1 MHz sine, its harmonics over the corners and temperatures; and a cross-coupled LC VCO on SG13G2, its
frequency and amplitude over its tank capacitance and three corners.

PAC on an NMOS passive mixer (LO 100 MHz, RF 110 MHz) over three corners: its sidebands -1, 0, +1 against TreeSPICE's
and ngspice's transients carrying a 1 mV tone. PNoise: with nothing periodic, the op-amp's equals .noise over five
corners (PSP103's noise through the periodic path); the VCO's phase noise falls at 20 dB/decade in its 1/df^2 region and
is converged in its sidebands. (No brute-force phase-noise reference yet: TreeSPICE has no transient noise sources.)
Exit 0 when every check passes.
"""
import argparse, cmath, math, os, shutil, sys, tempfile

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


def pss_bench(m):
    """The follower driven by a 0.6 V +- 0.3 V sine at 1 MHz: slewing and the input pair's swing make harmonics."""
    m("cell_create", library="design", cell="tb_pss")
    m("view_create", library="design", cell="tb_pss", view="schematic", type="schematic")
    m1.place(m, "design", "tb_pss", "I0", "design", "opamp", 0, 0, {"inp": "inp", "inn": "out", "ibias": "nb", "out": "out", "vdd": "vdd", "vss": "gnd!"})
    m1.place(m, "design", "tb_pss", "VDD", "analogLib", "vdc", -1500, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    m1.place(m, "design", "tb_pss", "IB", "analogLib", "idc", -2000, 0, {"PLUS": "vdd", "MINUS": "nb"}, {"idc": "10u"})
    m1.place(m, "design", "tb_pss", "CL", "analogLib", "cap", 1500, 0, {"PLUS": "out", "MINUS": "gnd!"}, {"c": "1p"})
    m1.place(m, "design", "tb_pss", "VIN", "analogLib", "vsin", -2500, 0, {"PLUS": "inp", "MINUS": "gnd!"}, {"vdc": "0.6", "va": "0.3", "freq": "1meg"})


def vco_bench(m):
    """A cross-coupled NMOS LC VCO: 2 nH and {ct} per side with 300 Ohm of tank loss, a 3 mA tail. A 0.2 V pulse under
    one tank capacitor kicks it at 0.1 ns (its period 1 s: still while the oscillator is shot)."""
    m("cell_create", library="design", cell="tb_vco")
    m("view_create", library="design", cell="tb_vco", view="schematic", type="schematic")
    parts = [("VDD", "analogLib", "vdc", {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"}),
             ("L1", "analogLib", "ind", {"PLUS": "vdd", "MINUS": "outp"}, {"l": "2n"}),
             ("L2", "analogLib", "ind", {"PLUS": "vdd", "MINUS": "outn"}, {"l": "2n"}),
             ("R1", "analogLib", "res", {"PLUS": "vdd", "MINUS": "outp"}, {"r": "300"}),
             ("R2", "analogLib", "res", {"PLUS": "vdd", "MINUS": "outn"}, {"r": "300"}),
             ("C1", "analogLib", "cap", {"PLUS": "outp", "MINUS": "gnd!"}, {"c": "{ct}"}),
             ("C2", "analogLib", "cap", {"PLUS": "outn", "MINUS": "kick"}, {"c": "{ct}"}),
             ("VK", "analogLib", "vpulse", {"PLUS": "kick", "MINUS": "gnd!"},
              {"v1": "0", "v2": "0.2", "td": "0.1n", "tr": "20p", "tf": "20p", "pw": "0.1n", "per": "1"}),
             ("M1", "sg13g2", "sg13_lv_nmos", {"D": "outp", "G": "outn", "S": "tail", "B": "gnd!"}, {"w": "20u", "l": "0.13u"}),
             ("M2", "sg13g2", "sg13_lv_nmos", {"D": "outn", "G": "outp", "S": "tail", "B": "gnd!"}, {"w": "20u", "l": "0.13u"}),
             ("IT", "analogLib", "idc", {"PLUS": "tail", "MINUS": "gnd!"}, {"idc": "3m"})]
    for k, (name, lib, master, nets, params) in enumerate(parts):
        m1.place(m, "design", "tb_vco", name, lib, master, 600 * (k % 6), -600 * (k // 6), nets, params)


def mixer_bench(m):
    """An NMOS passive mixer: RF through 50 Ohm into the drain, the IF at the source on 1 kOhm || 1 pF, the gate driven
    0.6 V +- 0.6 V at 100 MHz. The RF source: AC 1 (pac) and a {rfa} sine at 110 MHz (the transients' tone)."""
    m("cell_create", library="design", cell="tb_mix")
    m("view_create", library="design", cell="tb_mix", view="schematic", type="schematic")
    parts = [("VLO", "analogLib", "vsin", {"PLUS": "lo", "MINUS": "gnd!"}, {"vdc": "0.6", "va": "0.6", "freq": "100meg"}),
             ("VRF", "analogLib", "vsin", {"PLUS": "rfs", "MINUS": "gnd!"}, {"vdc": "0", "va": "{rfa}", "freq": "110meg", "acm": "1"}),
             ("RS", "analogLib", "res", {"PLUS": "rfs", "MINUS": "rf"}, {"r": "50"}),
             ("M1", "sg13g2", "sg13_lv_nmos", {"D": "rf", "G": "lo", "S": "if", "B": "gnd!"}, {"w": "10u", "l": "0.13u"}),
             ("RL", "analogLib", "res", {"PLUS": "if", "MINUS": "gnd!"}, {"r": "1k"}),
             ("CL", "analogLib", "cap", {"PLUS": "if", "MINUS": "gnd!"}, {"c": "1p"})]
    for k, (name, lib, master, nets, params) in enumerate(parts):
        m1.place(m, "design", "tb_mix", name, lib, master, 600 * k, 0, nets, params)


def follower_bench(m):
    """The op-amp as a follower at 0.6 V, its input AC 1: every mode fast (tb_ol's 1 GOhm / 1 F is a 1e9 s mode)."""
    m("cell_create", library="design", cell="tb_fol")
    m("view_create", library="design", cell="tb_fol", view="schematic", type="schematic")
    m1.place(m, "design", "tb_fol", "I0", "design", "opamp", 0, 0, {"inp": "inp", "inn": "out", "ibias": "nb", "out": "out", "vdd": "vdd", "vss": "gnd!"})
    m1.place(m, "design", "tb_fol", "VDD", "analogLib", "vdc", -1500, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
    m1.place(m, "design", "tb_fol", "IB", "analogLib", "idc", -2000, 0, {"PLUS": "vdd", "MINUS": "nb"}, {"idc": "10u"})
    m1.place(m, "design", "tb_fol", "CL", "analogLib", "cap", 1500, 0, {"PLUS": "out", "MINUS": "gnd!"}, {"c": "1p"})
    m1.place(m, "design", "tb_fol", "VIN", "analogLib", "vdc", -2500, 0, {"PLUS": "inp", "MINUS": "gnd!"}, {"vdc": "0.6", "acm": "1"})


def tone(x, y, f, T):
    """The phasor at f of a transient's last T (a whole number of f's periods), on 4000 equal ticks"""
    s = last_period(x, y, T, 4000)
    return sum(v * cmath.exp(-2j * math.pi * f * (x[-1] - T + i * T / 4000)) for i, v in enumerate(s)) * 2 / 4000


def last_period(x, y, T, n=1000):
    """A transient's last period, [x_end - T, x_end), on n equal ticks (linear between its points)"""
    out, k, t0 = [], 1, x[-1] - T
    for i in range(n):
        t = t0 + i * T / n
        while k + 1 < len(x) and x[k] < t: k += 1
        out.append(y[k - 1] + (t - x[k - 1]) / (x[k] - x[k - 1]) * (y[k] - y[k - 1]))
    return out


def harmonics(samples, harms):
    """The peak phasors of a period's equal ticks, V_0 the mean (as TreeSPICE's PSS Harmonics)"""
    n = len(samples)
    return [sum(v * cmath.exp(-2j * math.pi * k * i / n) for i, v in enumerate(samples)) * ((1 if k == 0 else 2) / n) for k in range(harms + 1)]


def frequency(x, y, periods=20):
    """From a transient's last `periods` rising crossings of its mean (over those periods)"""
    rises = [x[k - 1] + (0 - y[k - 1]) / (y[k] - y[k - 1]) * (x[k] - x[k - 1]) for k in range(1, len(x)) if y[k - 1] < 0 <= y[k]]
    return periods / (rises[-1] - rises[-1 - periods])


def wave(m, run, expression):
    """A complex waveform from wave_eval: (x, [complex])"""
    w = m("wave_eval", run=run, expression=expression)
    return w["x"], [complex(r, i) for r, i in zip(w["re"], w["im"])] if "re" in w else [complex(y, 0) for y in w["y"]]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mcp", required=True)
    ap.add_argument("--pdk", required=True, help="the ihp-sg13g2 directory")
    ap.add_argument("--osdi", required=True, help="psp103.osdi")
    ap.add_argument("--ngspice", default="ngspice")
    ap.add_argument("--keep", help="build the workspace here (kept) instead of a temporary directory")
    a = ap.parse_args()
    osdi_dir = tempfile.mkdtemp(prefix="m2osdi")  # ngspice's osdi command takes no path with spaces
    osdi_copy = os.path.join(osdi_dir, "psp103.osdi")
    shutil.copy(a.osdi, osdi_copy)
    ws = a.keep or tempfile.mkdtemp(prefix="m2 gate ")
    os.makedirs(ws, exist_ok=True)
    m = m1.Mcp(os.path.abspath(a.mcp), os.path.join(ws, "openvlsi.libs"))
    print(f"workspace: {ws}")

    print("1. the op-amp, its open-loop bench and a follower with a probe in its loop")
    pdk = m("pdk_install", pdk="ihp-sg13g2", root=os.path.abspath(a.pdk), osdi=os.path.abspath(a.osdi))
    m1.build(m)
    for cell, inj in (("tb_stb", None), ("tb_injv", "v"), ("tb_inji", "i"), ("tb_stbbuf", "buffered")):
        stb_bench(m, cell, inj)
    broken_bench(m)
    pss_bench(m)
    vco_bench(m)
    mixer_bench(m)
    follower_bench(m)
    benches = ("tb_stb", "tb_injv", "tb_inji", "tb_stbbuf", "tb_broken", "tb_pss", "tb_vco", "tb_mix", "tb_fol")
    check(all(m("schematic_check", library="design", cell=c, view="schematic")["ok"] for c in benches),
          "the stb, injection, driven PSS, VCO, mixer and follower benches check clean")

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

    print("3. PSS: the follower driven at 1 MHz, over five corners and three temperatures, against long transients")
    m("cell_create", library="design", cell="pssbench")
    m("view_create", library="design", cell="pssbench", view="ade", type="maestro")
    view = dict(library="design", cell="pssbench", view="ade")
    H = 5
    m("ade_setup_set", setup={
        "tests": [{"name": "pss", "design": "design/tb_pss/schematic",
                   "analyses": [{"type": "pss", "fund": "1meg", "tstab": "5u", "oscnode": "0", "points": "1000", "harms": str(H)}]},
                  {"name": "tran", "design": "design/tb_pss/schematic", "analyses": [{"type": "tran", "step": "1n", "stop": "30u", "start": "0", "maxstep": "1n"}]}],
        "temperature": "-40 27 125",
        "corners": [{"name": c, "models": [{"file": lv, "section": "mos_" + c}]} for c in ("ss", "ff", "sf", "fs")],
        "outputs": [{"name": "h1", "test": "pss", "expression": "mag(harmonic(VH(out), 1))"},
                    {"name": "thd", "test": "pss", "expression": "thd(VH(out))"}]}, **view)
    h = m("ade_run", **view)
    pts = h["points"]
    check(h["status"] == "done" and len(pts) == 2 * 15 and all(p["status"] == "done" for p in pts), f"{len(pts)} points ran")
    dirs = {}
    for p in pts:
        dirs.setdefault((p["corner"], p["temperature"]), {})[p["test"]] = p
    worst = {"ts": (0, None), "ng": (0, None)}
    thds = []
    for key, d in dirs.items():
        _, vh = wave(m, d["pss"]["dir"], "VH(out)")
        thds.append(d["pss"]["outputs"]["thd"]["value"])
        refs = {"ts": d["tran"]["dir"], "ng": m1.ngspice(d["tran"]["dir"], osdi_copy, a.ngspice)}
        for who, run in refs.items():
            x, y = wave(m, run, "VT(out)")
            ref = harmonics(last_period(x, [v.real for v in y], 1e-6), H)
            err = max(abs(vh[k] - ref[k]) for k in range(H + 1)) / abs(ref[1])  # every harmonic, against the first
            if err >= worst[who][0]: worst[who] = (err, key)
    check(min(thds) > 0.1, f"the drive distorts (thd {min(thds):.3g} .. {max(thds):.3g} %): the harmonics are worth comparing")
    for who, name in (("ts", "TreeSPICE's"), ("ng", "ngspice's")):
        err, key = worst[who]
        check(err <= 1e-3, f"PSS harmonics 0 .. {H} match {name} 30 us transient's last period to {err:.2e} of the first ({key[0]} {key[1]} C at worst)")

    print("4. PSS: an LC VCO on SG13G2, its period an unknown, over its tank capacitance and three corners")
    m("cell_create", library="design", cell="vcobench")
    m("view_create", library="design", cell="vcobench", view="ade", type="maestro")
    view = dict(library="design", cell="vcobench", view="ade")
    m("ade_setup_set", setup={
        "variables": {"ct": "0.5p 1p"},
        "tests": [{"name": "pss", "design": "design/tb_vco/schematic",
                   "analyses": [{"type": "pss", "fund": "3G", "tstab": "20n", "oscnode": "outp", "points": "400", "harms": "5"}]},
                  {"name": "tran", "design": "design/tb_vco/schematic", "analyses": [{"type": "tran", "step": "1p", "stop": "40n", "start": "0", "maxstep": "1p"}]}],
        "temperature": "27",
        "corners": [{"name": c, "models": [{"file": lv, "section": "mos_" + c}]} for c in ("ss", "ff")],
        "outputs": [{"name": "f", "test": "pss", "expression": "fundamental(VH(outp))"},
                    {"name": "swing", "test": "pss", "expression": "ymax(VPSS(outp) - VPSS(outn))"}]}, **view)
    h = m("ade_run", **view)
    pts = h["points"]
    check(h["status"] == "done" and len(pts) == 2 * 3 * 2 and all(p["status"] == "done" for p in pts), f"{len(pts)} points ran")
    dirs = {}
    for p in pts:
        dirs.setdefault((p["corner"], p["variables"]["ct"]), {})[p["test"]] = p
    worst = {"ts": (0, None, 0, 0), "ng": (0, None, 0, 0)}
    # ngspice's swing is first order in its step here: 0.95, 0.61, 0.27 mV above TreeSPICE's at 1, 0.5 and 0.25 ps
    # (nominal, ct 0.5p; both settled), TreeSPICE's own moving 8 uV. Its looser bound is that step error, not PSS's
    tol_swing = {"ts": 1e-3, "ng": 2e-3}
    fs = []
    for key, d in dirs.items():
        f, swing = d["pss"]["outputs"]["f"]["value"], d["pss"]["outputs"]["swing"]["value"]
        fs.append(f)
        refs = {"ts": d["tran"]["dir"], "ng": m1.ngspice(d["tran"]["dir"], osdi_copy, a.ngspice)}
        for who, run in refs.items():
            x, y = wave(m, run, "VT(outp) - VT(outn)")
            y = [v.real for v in y]
            ft = frequency(x, y)
            amp = max(v for t, v in zip(x, y) if t > x[-1] - 20 / ft)
            err = max(abs(f / ft - 1) / 1e-4, abs(swing / amp - 1) / tol_swing[who])  # in tolerances
            if err >= worst[who][0]: worst[who] = (err, key, f / ft - 1, swing / amp - 1)
    check(max(fs) / min(fs) > 1.2, f"the tank capacitance tunes it: {min(fs) / 1e9:.4g} .. {max(fs) / 1e9:.4g} GHz")
    vco_dirs = dirs
    for who, name in (("ts", "TreeSPICE's"), ("ng", "ngspice's")):
        err, key, df, da = worst[who]
        check(err <= 1, f"the VCO's PSS frequency and swing match {name} 40 ns transient's last 20 periods to 1e-4 and "
                        f"{tol_swing[who]:g} (worst {df:+.2e} / {da:+.2e}, {key[0]}, ct {key[1]})")

    print("5. PAC: an NMOS passive mixer, against transients with a 1 mV tone, over three corners")
    m("cell_create", library="design", cell="mixbench")
    m("view_create", library="design", cell="mixbench", view="ade", type="maestro")
    view = dict(library="design", cell="mixbench", view="ade")
    tran = {"type": "tran", "step": "10p", "stop": "300n", "start": "0", "maxstep": "10p"}
    m("ade_setup_set", setup={
        "variables": {"rfa": "0"},
        "tests": [{"name": "pac", "design": "design/tb_mix/schematic",
                   "analyses": [{"type": "pss", "fund": "100meg", "tstab": "0", "oscnode": "0", "points": "400", "harms": "5"},
                                {"type": "pac", "sweep": "lin", "points": "3", "start": "100meg", "stop": "120meg", "sidebands": "20"}]},
                  {"name": "tone", "design": "design/tb_mix/schematic", "variables": {"rfa": "1m"}, "analyses": [tran]},
                  {"name": "lo", "design": "design/tb_mix/schematic", "analyses": [tran]}],
        "temperature": "27",
        "corners": [{"name": c, "models": [{"file": lv, "section": "mos_" + c}]} for c in ("ss", "ff")],
        "outputs": [{"name": "gc", "test": "pac", "expression": "dB20(value(VPAC(if, -1), 110meg))"}]}, **view)
    h = m("ade_run", **view)
    pts = h["points"]
    check(h["status"] == "done" and len(pts) == 3 * 3 and all(p["status"] == "done" for p in pts), f"{len(pts)} points ran")
    dirs = {}
    for p in pts:
        dirs.setdefault(p["corner"], {})[p["test"]] = p
    worst = {"ts": (0, None), "ng": (0, None)}
    gains = []
    for corner, d in dirs.items():
        gains.append(d["pac"]["outputs"]["gc"]["value"])
        refs = {"ts": (d["tone"]["dir"], d["lo"]["dir"]),
                "ng": (m1.ngspice(d["tone"]["dir"], osdi_copy, a.ngspice), m1.ngspice(d["lo"]["dir"], osdi_copy, a.ngspice))}
        for sb in (-1, 0, 1):
            fx, pac = wave(m, d["pac"]["dir"], f"VPAC(if, {sb})")
            want = pac[fx.index(min(fx, key=lambda f: abs(f - 110e6)))] * -1j  # the tone is a sine: -j times the cosine phasor
            f = 110e6 + sb * 100e6
            for who, (run, lo_run) in refs.items():
                x, y = wave(m, run, "VT(if)")
                xl, yl = wave(m, lo_run, "VT(if)")
                got = (tone(x, [v.real for v in y], f, 100e-9) - tone(xl, [v.real for v in yl], f, 100e-9)) / 1e-3
                err = abs(got / want - 1)
                if err >= worst[who][0]: worst[who] = (err, (corner, sb))
    check(all(-20 < g < 0 for g in gains), f"a conversion loss: {min(gains):.3f} .. {max(gains):.3f} dB to the 10 MHz IF")
    for who, name in (("ts", "TreeSPICE's"), ("ng", "ngspice's")):
        err, key = worst[who]
        check(err <= 2e-3, f"PAC's sidebands -1, 0, +1 match {name} transient with the tone to {err:.2e} ({key[0]}, sideband {key[1]} at worst)")

    print("6. PNoise: nothing periodic is .noise (the op-amp, five corners); the VCO's phase noise")
    m("cell_create", library="design", cell="pnbench")
    m("view_create", library="design", cell="pnbench", view="ade", type="maestro")
    view = dict(library="design", cell="pnbench", view="ade")
    nsweep = {"sweep": "dec", "points": "5", "start": "1k", "stop": "100meg"}
    m("ade_setup_set", setup={
        "tests": [{"name": "pn", "design": "design/tb_fol/schematic",
                   "analyses": [{"type": "pss", "fund": "1meg", "tstab": "0", "oscnode": "0", "points": "64", "harms": "5"},
                                dict({"type": "pnoise", "output": "v(out)", "sidebands": "3"}, **nsweep)]},
                  {"name": "nz", "design": "design/tb_fol/schematic", "analyses": [{"type": "op"}, dict({"type": "noise", "output": "v(out)", "source": "VIN"}, **nsweep)]},
                  {"name": "slow", "design": "design/tb_ol/schematic",
                   "analyses": [{"type": "pss", "fund": "1meg", "tstab": "0", "oscnode": "0", "points": "64", "harms": "5"}]}],
        "temperature": "27",
        "corners": [{"name": c, "models": [{"file": lv, "section": "mos_" + c}]} for c in ("ss", "ff", "sf", "fs")],
        "outputs": []}, **view)
    h = m("ade_run", **view)
    pts = h["points"]
    slow = [p for p in pts if p["test"] == "slow"]
    pts = [p for p in pts if p["test"] != "slow"]
    check(h["status"] == "done" and len(pts) == 10 and all(p["status"] == "done" for p in pts), f"{len(pts)} points ran")
    check(slow and all(p["status"] == "error" and "a mode far slower than the period" in p["error"] for p in slow),
          "the open-loop bench's 1 GOhm / 1 F mode (1e9 s against a 1 us period) is refused with its reason, not shot through")
    dirs = {}
    for p in pts:
        dirs.setdefault(p["corner"], {})[p["test"]] = p["dir"]
    worst = (0, None)
    for corner, d in dirs.items():
        _, pn = wave(m, d["pn"], "VPN(out)")
        _, nz = wave(m, d["nz"], "VN(out)")
        err = max(abs(a_ / b_ - 1) for a_, b_ in zip(pn, nz))
        if err >= worst[0]: worst = (err, corner)
    # PNoise linearises about the PSS's state, the DC operating point to Newton's tolerance (a linear RC's is exact)
    check(worst[0] <= 1e-4, f"the op-amp's PNoise with nothing periodic is its .noise at every frequency (worst {worst[0]:.1e}, {worst[1]})")
    m("cell_create", library="design", cell="vcopn")
    m("view_create", library="design", cell="vcopn", view="ade", type="maestro")
    view = dict(library="design", cell="vcopn", view="ade")
    pss = {"type": "pss", "fund": "3G", "tstab": "20n", "oscnode": "outp", "points": "400", "harms": "5"}
    psweep = {"type": "pnoise", "output": "v(outp)", "sweep": "dec", "points": "1", "start": "1meg", "stop": "10meg", "relharm": "1"}
    pn_db = "dB20(VPN(out) / (mag(harmonic(VH(outp), 1)) / sqrt(2)))"  # dBc/Hz against the carrier's power
    m("ade_setup_set", setup={
        "variables": {"ct": "1p"},
        "tests": [{"name": k, "design": "design/tb_vco/schematic", "analyses": [pss, dict(psweep, sidebands=str(sb))]} for k, sb in (("k20", 20), ("k40", 40))],
        "temperature": "27",
        "outputs": [{"name": f"{k}_{f}", "test": k, "expression": f"value({pn_db}, {f})"} for k in ("k20", "k40") for f in ("1meg", "10meg")]}, **view)
    h = m("ade_run", **view)
    o = {}
    for p in h["points"]:
        o.update({n: v.get("value") for n, v in p.get("outputs", {}).items()})
    names = [f"{k}_{f}" for k in ("k20", "k40") for f in ("1meg", "10meg")]
    check(h["status"] == "done" and all(isinstance(o.get(n), float) for n in names), "the VCO's phase noise ran at 20 and 40 sidebands")
    conv = max(abs(o[f"k40_{f}"] - o[f"k20_{f}"]) for f in ("1meg", "10meg"))
    check(conv <= 0.2, f"converged in its sidebands: 20 and 40 agree to {conv:.3f} dB ({o['k40_1meg']:.2f} dBc/Hz at 1 MHz, "
                       f"{o['k40_10meg']:.2f} at 10 MHz)")
    slope = o["k40_1meg"] - o["k40_10meg"]
    check(abs(slope - 20) <= 1.5, f"in its 1/df^2 region: {slope:.2f} dB per decade from 1 to 10 MHz")
    m.close()
    failed = [w for ok, w in m1.CHECKS if not ok]
    print(f"\n{'PASSED' if not failed else 'FAILED'}: {len(m1.CHECKS) - len(failed)} of {len(m1.CHECKS)} checks")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
