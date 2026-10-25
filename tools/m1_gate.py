#!/usr/bin/env python3
"""The M1 gate (docs/rebuild.md), driven through the MCP server alone.

    python3 tools/m1_gate.py --mcp build/openvlsi-mcp --pdk <ihp-sg13g2> --osdi <psp103.osdi> [--ngspice ngspice] [--keep DIR]

From an empty workspace: install the SG13G2 PDK; build a two-stage Miller op-amp (schematic, symbol) and two test
benches that use the symbol; set up ADE (op, ac, noise and a closed-loop step, over the five MOS corners and a
temperature sweep; outputs with specs); run it, then a 200-sample Monte Carlo (process and mismatch). Every corner
point's netlist is run again on ngspice and every output compared, evaluated by the same calculator; the Monte Carlo
is compared by distribution, a two-sample Kolmogorov-Smirnov test per output (the simulators' random generators
differ, so their samples do). Exit 0 when every check passes.
"""
import argparse, json, math, os, re, shutil, subprocess, sys, tempfile

CHECKS = []


def check(ok, what):
    CHECKS.append((bool(ok), what))
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}", flush=True)
    return ok


class Mcp:
    """openvlsi-mcp over stdio: one JSON-RPC request per line."""

    def __init__(self, exe, workspace):
        self.p = subprocess.Popen([exe, "--workspace", workspace], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.n = 0
        self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "m1_gate", "version": "1"}})
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
        except json.JSONDecodeError:  # a string result comes as its text
            return text

    def close(self):
        self.p.stdin.close()
        self.p.wait(timeout=30)


# ── the design ─────────────────────────────────────────────────────────────────────────────────────────────────────
def place(m, lib, cell, name, master_lib, master, x, y, nets, params=None):
    """An instance whose pins get their nets by labels on them (nets: pin -> net)."""
    m("instance_add", library=lib, cell=cell, view="schematic", master_library=master_lib, master_cell=master, name=name, x=x, y=y,
      params=params or {})
    for p in m("view_get", library=master_lib, cell=master, view="symbol")["pins"]:
        if p["name"] in nets:
            m("label_add", library=lib, cell=cell, view="schematic", name=nets[p["name"]], x=x + p["at"][0], y=y + p["at"][1])


def build(m):
    m("library_create", library="design", path="design.oalib")
    # the op-amp: an NMOS pair with a PMOS mirror, a PMOS common-source second stage, Miller compensation
    m("cell_create", library="design", cell="opamp")
    m("view_create", library="design", cell="opamp", view="schematic", type="schematic")
    for k, (pin, d) in enumerate([("inp", "input"), ("inn", "input"), ("ibias", "input"), ("out", "output"), ("vdd", "inputOutput"),
                                  ("vss", "inputOutput")]):
        m("pin_add", library="design", cell="opamp", view="schematic", name=pin, direction=d, x=-2000, y=1000 - 300 * k)
    L = "0.5u"
    mos = [("M1", "sg13_lv_nmos", "x", "inn", "tail", "vss", "4u"), ("M2", "sg13_lv_nmos", "y", "inp", "tail", "vss", "4u"),
           ("M3", "sg13_lv_pmos", "x", "x", "vdd", "vdd", "8u"), ("M4", "sg13_lv_pmos", "y", "x", "vdd", "vdd", "8u"),
           ("M5", "sg13_lv_nmos", "tail", "ibias", "vss", "vss", "8u"), ("M8", "sg13_lv_nmos", "ibias", "ibias", "vss", "vss", "4u"),
           ("M6", "sg13_lv_pmos", "out", "y", "vdd", "vdd", "32u"), ("M7", "sg13_lv_nmos", "out", "ibias", "vss", "vss", "16u")]
    for k, (name, dev, d, g, s, b, w) in enumerate(mos):
        place(m, "design", "opamp", name, "sg13g2", dev, 600 * (k % 4), -600 * (k // 4), {"D": d, "G": g, "S": s, "B": b}, {"w": w, "l": L})
    place(m, "design", "opamp", "CC", "analogLib", "cap", 2600, 0, {"PLUS": "y", "MINUS": "out"}, {"c": "0.5p"})
    m("symbol_generate", library="design", cell="opamp")

    def bench(cell, closed):
        m("cell_create", library="design", cell=cell)
        m("view_create", library="design", cell=cell, view="schematic", type="schematic")
        nets = {"inp": "inp", "inn": "out" if closed else "inn", "ibias": "nb", "out": "out", "vdd": "vdd", "vss": "gnd!"}
        place(m, "design", cell, "I0", "design", "opamp", 0, 0, nets)
        place(m, "design", cell, "VDD", "analogLib", "vdc", -1500, 0, {"PLUS": "vdd", "MINUS": "gnd!"}, {"vdc": "1.2"})
        place(m, "design", cell, "IB", "analogLib", "idc", -2000, 0, {"PLUS": "vdd", "MINUS": "nb"}, {"idc": "10u"})
        place(m, "design", cell, "CL", "analogLib", "cap", 1500, 0, {"PLUS": "out", "MINUS": "gnd!"}, {"c": "1p"})
        if closed:  # a unity-gain follower stepped from 0.5 to 0.7 V
            place(m, "design", cell, "VIN", "analogLib", "vpulse", -2500, 0, {"PLUS": "inp", "MINUS": "gnd!"},
                  {"v1": "0.5", "v2": "0.7", "td": "50n", "tr": "1n", "tf": "1n", "pw": "2u", "per": "4u"})
        else:  # open loop for AC, its DC point held by a 1 GΩ / 1 F feedback low-pass
            place(m, "design", cell, "VIN", "analogLib", "vdc", -2500, 0, {"PLUS": "inp", "MINUS": "gnd!"}, {"vdc": "0.6", "acm": "1"})
            place(m, "design", cell, "RF", "analogLib", "res", 2000, 0, {"PLUS": "out", "MINUS": "inn"}, {"r": "1G"})
            place(m, "design", cell, "CF", "analogLib", "cap", 2500, 0, {"PLUS": "inn", "MINUS": "gnd!"}, {"c": "1"})

    bench("tb_ol", False)
    bench("tb_cl", True)


# ── ADE ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
# each output, and how closely ngspice must agree: |TreeSPICE - ngspice| <= max(abs, rel * |ngspice|)
OUTPUTS = [
    ("gain", "ol", "dB20(value(VF(out), 10))", {"min": "40"}, 0.05, 0),          # dB
    ("ugf", "ol", "ugf(VF(out))", {}, 0, 0.01),
    ("pm", "ol", "phaseMargin(VF(out))", {"min": "45"}, 0.5, 0),               # degrees
    ("vout", "ol", "VOP(out)", {}, 1e-3, 0),
    ("idd", "ol", "-IOP(VDD)", {}, 0, 0.005),
    ("noise", "ol", "value(VN(out), 1k)", {}, 0, 0.02),
    ("slew", "cl", "slewRate(VT(out))", {}, 0, 0.02),
    ("settle", "cl", "settlingTime(VT(out), 1)", {}, 5e-9, 0.03),
    ("step", "cl", "value(VT(out), 1.4u) - value(VT(out), 40n)", {}, 1e-3, 0),
]


def setup(pdk, samples):
    lv = pdk["models"]["mos_lv"]
    return {
        "tests": [
            {"name": "ol", "design": "design/tb_ol/schematic",
             "analyses": [{"type": "op"}, {"type": "ac", "sweep": "dec", "points": "20", "start": "1", "stop": "10G"},
                          {"type": "noise", "output": "v(out)", "source": "VIN", "sweep": "dec", "points": "10", "start": "1", "stop": "100meg"}]},
            {"name": "cl", "design": "design/tb_cl/schematic", "analyses": [{"type": "tran", "step": "1n", "stop": "1.5u", "start": "0", "maxstep": "0.2n"}]},
        ],
        "temperature": "-40 27 125",
        "corners": [{"name": c, "models": [{"file": lv, "section": "mos_" + c}]} for c in ("ss", "ff", "sf", "fs")],
        "outputs": [dict({"name": n, "test": t, "expression": e}, **spec) for n, t, e, spec, _, _ in OUTPUTS],
        "montecarlo": {"samples": samples, "seed": 1, "temperature": "27", "models": [{"file": lv, "section": "mos_tt_stat_mismatch"}], "variables": {"mc_mismatch": "1"}},
    }


def ngspice(point_dir, osdi_copy, exe):
    """The point's netlist on ngspice, in <point>/ngspice: its raw file there (OSDI through .spiceinit's osdi)."""
    d = os.path.join(point_dir, "ngspice")
    os.makedirs(d, exist_ok=True)
    deck = open(os.path.join(point_dir, "input.cir")).read()
    open(os.path.join(d, "input.cir"), "w").write(re.sub(r"(?im)^\s*\.osdi\b.*\n?", "", deck))
    open(os.path.join(d, ".spiceinit"), "w").write(f"osdi {osdi_copy}\n")
    p = subprocess.run([exe, "-b", "-r", "out.raw", "input.cir"], cwd=d, capture_output=True, text=True, timeout=600)
    if p.returncode != 0 or not os.path.exists(os.path.join(d, "out.raw")):
        raise RuntimeError(f"ngspice failed in {d}: " + " | ".join((p.stdout + p.stderr).strip().splitlines()[-4:]))
    return d


def bundle(app, ws):
    """Every Mach-O file in the bundle links only the system's libraries and the bundle's own; the app starts in an
    empty environment (no Homebrew on PATH, no Qt variables) and stays up. ponytail: a stand-in for a clean macOS
    account, which only a person can make; the owner runs it there once."""
    foreign = []
    for root, _, files in os.walk(app):
        for f in files:
            p = os.path.join(root, f)
            if os.path.islink(p) or not os.access(p, os.R_OK):
                continue
            out = subprocess.run(["otool", "-L", p], capture_output=True, text=True).stdout
            if "is not an object file" in out or not out:
                continue
            own = subprocess.run(["otool", "-D", p], capture_output=True, text=True).stdout.splitlines()[1:]  # a dylib's own name
            for line in out.splitlines()[1:]:
                lib = line.strip().split(" (")[0]
                if lib in own:
                    continue
                if lib and not lib.startswith(("/System/", "/usr/lib/", "@rpath", "@executable_path", "@loader_path")):
                    foreign.append(f"{os.path.relpath(p, app)} -> {lib}")
            # an @rpath is searched along the file's LC_RPATHs: one outside the bundle reaches the machine's libraries
            cmds = subprocess.run(["otool", "-l", p], capture_output=True, text=True).stdout.splitlines()
            for k, line in enumerate(cmds):
                if line.strip() == "cmd LC_RPATH" and k + 2 < len(cmds):
                    rpath = cmds[k + 2].strip().split(" (offset")[0].removeprefix("path ")
                    if not rpath.startswith("@"):
                        foreign.append(f"{os.path.relpath(p, app)} rpath {rpath}")
    check(not foreign, "the bundle links nothing outside itself and the system (load paths and rpaths)" +
          (f" ({len(foreign)} leaks, first: {foreign[0]})" if foreign else ""))
    sim = os.path.join(app, "Contents", "MacOS", "treespice")
    deck = os.path.join(tempfile.mkdtemp(prefix="m1sim"), "d.cir")
    open(deck, "w").write("* divider\nV1 a 0 dc 2\nR1 a b 1k\nR2 b 0 1k\n.op\n.end\n")
    out = subprocess.run([sim, "-b", deck], capture_output=True, text=True, env={"HOME": tempfile.gettempdir(), "PATH": "/usr/bin:/bin"}).stdout \
        if os.path.exists(sim) else ""
    check(re.search(r"^\s*b\s+1\.0+e\+00", out, re.M), "the bundle's own simulator (treespice) runs a deck in an empty environment")
    signed = subprocess.run(["codesign", "--verify", "--deep", "--strict", app], capture_output=True, text=True)
    check(signed.returncode == 0, "the bundle's signature verifies (ad hoc)" + ("" if signed.returncode == 0 else f": {signed.stderr.strip()[-200:]}"))
    home = tempfile.mkdtemp(prefix="m1home")
    p = subprocess.Popen([os.path.join(app, "Contents", "MacOS", "OpenVLSI"), os.path.join(home, "openvlsi.libs")],
                         env={"HOME": home, "PATH": "/usr/bin:/bin"}, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        p.wait(timeout=4)
        alive = False
    except subprocess.TimeoutExpired:
        alive = True
        p.terminate()
    log = p.communicate(timeout=10)[0]
    check(alive and "plugin" not in log.lower(), "the app starts in an empty environment and stays up" + ("" if alive else f": {log.strip()[-300:]}"))


def ks(a, b):
    """The two-sample Kolmogorov-Smirnov statistic: the largest gap between the two empirical distributions."""
    a, b = sorted(a), sorted(b)
    i = j = 0
    d = 0.0
    while i < len(a) and j < len(b):
        x = min(a[i], b[j])
        while i < len(a) and a[i] == x: i += 1
        while j < len(b) and b[j] == x: j += 1
        d = max(d, abs(i / len(a) - j / len(b)))
    return d


def value(m, run, expression):
    v = m("wave_eval", run=run, expression=expression)
    if not isinstance(v, (int, float)):
        raise RuntimeError(f"{expression} is not a number: {str(v)[:80]}")
    return float(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mcp", required=True)
    ap.add_argument("--pdk", required=True, help="the ihp-sg13g2 directory")
    ap.add_argument("--osdi", required=True, help="psp103.osdi")
    ap.add_argument("--ngspice", default="ngspice")
    ap.add_argument("--keep", help="build the workspace here (kept) instead of a temporary directory")
    ap.add_argument("--samples", type=int, default=200)
    ap.add_argument("--bundle", help="the deployed OpenVLSI.app (cmake --build build --target bundle): checked self-contained")
    a = ap.parse_args()
    ws = a.keep or tempfile.mkdtemp(prefix="m1 gate ")
    os.makedirs(ws, exist_ok=True)
    osdi_dir = tempfile.mkdtemp(prefix="m1osdi")  # ngspice's osdi command takes no path with spaces
    osdi_copy = os.path.join(osdi_dir, "psp103.osdi")
    shutil.copy(a.osdi, osdi_copy)
    m = Mcp(os.path.abspath(a.mcp), os.path.join(ws, "openvlsi.libs"))
    print(f"workspace: {ws}")

    print("1. the design, from an empty workspace")
    pdk = m("pdk_install", pdk="ihp-sg13g2", root=a.pdk, osdi=a.osdi)
    check(pdk["libraries"] == ["analogLib", "sg13g2"], "pdk_install: analogLib and sg13g2")
    build(m)
    check(all(m("schematic_check", library="design", cell=c, view="schematic")["ok"] for c in ("opamp", "tb_ol", "tb_cl")),
          "a two-stage Miller op-amp (schematic, symbol) and two test benches using its symbol, all checking clean")

    print("2. ADE: op, ac, noise and a step, over five corners and a temperature sweep")
    m("cell_create", library="design", cell="bench")
    m("view_create", library="design", cell="bench", view="ade", type="maestro")
    view = dict(library="design", cell="bench", view="ade")
    m("ade_setup_set", setup=setup(pdk, a.samples), **view)
    h = m("ade_run", **view)
    points = h["points"]
    check(h["status"] == "done" and len(points) == 2 * 5 * 3 and all(p["status"] == "done" for p in points),
          f"{len(points)} points (2 tests x 5 corners x 3 temperatures) ran")
    errors = [(p["index"], n, o["error"]) for p in points for n, o in p["outputs"].items() if "error" in o]
    check(not errors, "every output has a value" + (f" (first error: {errors[0]})" if errors else ""))
    check(all(o.get("steps") and o.get("equation") for p in points for o in p["outputs"].values()), "every output has its equation and derivation")

    print("3. ngspice on every point's netlist, every output compared")
    tol = {n: (ab, rel) for n, _, _, _, ab, rel in OUTPUTS}
    worst = {}
    for p in points:
        ng = ngspice(p["dir"], osdi_copy, a.ngspice)
        for n, o in p["outputs"].items():
            ref = value(m, ng, next(e for k, _, e, *_ in OUTPUTS if k == n))
            ab, rel = tol[n]
            err = abs(o["value"] - ref)
            allowed = max(ab, rel * abs(ref))
            if n not in worst or err / allowed > worst[n][0]:
                worst[n] = (err / allowed, o["value"], ref, p["corner"], p["temperature"])
    for n, (ratio, t, ref, corner, temp) in worst.items():
        check(ratio <= 1, f"{n}: TreeSPICE matches ngspice at every point (worst {t:.6g} vs {ref:.6g}, {corner} {temp} C, "
                          f"{ratio:.2f} of the tolerance)")

    print(f"4. Monte Carlo: {a.samples} samples, process and mismatch")
    mc = m("ade_run", mode="montecarlo", **view)
    stats = mc["statistics"]
    check(mc["status"] == "done" and all(p["status"] == "done" for p in mc["points"]) and len(mc["points"]) == 2 * a.samples,
          f"{len(mc['points'])} Monte Carlo points ran")
    ng_values = {}
    for p in mc["points"]:
        ng = ngspice(p["dir"], osdi_copy, a.ngspice)
        for n in p["outputs"]:
            ng_values.setdefault(n, []).append(value(m, ng, next(e for k, _, e, *_ in OUTPUTS if k == n)))
    ts_values = {}
    for p in mc["points"]:
        for n, o in p["outputs"].items():
            ts_values.setdefault(n, []).append(o["value"])
    for n, xs in ng_values.items():
        mu = sum(xs) / len(xs)
        sd = math.sqrt(sum((x - mu) ** 2 for x in xs) / (len(xs) - 1))
        st = stats[n]
        # the two samples drawn from one distribution: the two-sample Kolmogorov-Smirnov statistic within its 1% critical
        # value (both shape and place; a sigma ratio alone fails on the heavy tails mismatch gives a CMRR)
        d, crit = ks(ts_values[n], xs), 1.628 * math.sqrt(2 / len(xs))
        check(d <= crit and st["sd"] > 0, f"{n}: TreeSPICE's distribution matches ngspice's (KS D {d:.3f} <= {crit:.3f}; mean {st['mean']:.6g} vs "
                                         f"{mu:.6g}, sd {st['sd']:.4g} vs {sd:.4g})")

    m.close()
    if a.bundle:
        print("5. the app bundle, self-contained")
        bundle(a.bundle, ws)
    failed = [w for ok, w in CHECKS if not ok]
    json.dump({"workspace": ws, "checks": [{"ok": ok, "what": w} for ok, w in CHECKS]}, open(os.path.join(ws, "m1_gate.json"), "w"), indent=1)
    print(f"\n{'PASSED' if not failed else 'FAILED'}: {len(CHECKS) - len(failed)} of {len(CHECKS)} checks (report: {ws}/m1_gate.json)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
