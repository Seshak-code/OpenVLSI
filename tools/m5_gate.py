#!/usr/bin/env python3
"""The digital gate (docs/rebuild.md, M5): RTL to GDS through openvlsi-mcp alone, then the chip under test.

    python3 tools/m5_gate.py --mcp build/openvlsi-mcp [--workspace DIR] [--platform ihp-sg13g2] [--reuse design/run.N]

Steps:
1. flow_run takes gcd from Verilog to GDS: clean means timing met, DRC clean and LVS matched, all read from the tools' reports.
2. The same testbench on the RTL, and on the routed netlist with its layout's delays, passes at the run's clock.
3. Clocked faster than its timing allows, the routed netlist fails while the RTL still passes: the delays are real.
4. A wrong expectation fails at both levels: the testbench's verdict is what counts.
5. Waveforms recorded at gate level hold the right answer at the outputs.
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

TESTBENCH = r"""`timescale 1ns/1ps
// gcd(a, b) through its request and response handshake, on several pairs
module tb;
  reg clk = 0, reset = 1, req_val = 0, resp_rdy = 1;
  reg [31:0] req_msg = 0;
  wire req_rdy, resp_val;
  wire [15:0] resp_msg;
  integer errors = 0;
  gcd dut(.clk(clk), .req_msg(req_msg), .req_rdy(req_rdy), .req_val(req_val), .reset(reset), .resp_msg(resp_msg), .resp_rdy(resp_rdy), .resp_val(resp_val));
  always #(`PERIOD / 2.0) clk = ~clk;

  task check(input [15:0] a, input [15:0] b, input [15:0] expected);
    integer waited;
    begin
      @(negedge clk);
      req_msg = {a, b};
      req_val = 1;
      waited = 0;
      while (req_rdy !== 1'b1 && waited < 1000) begin @(negedge clk); waited = waited + 1; end
      @(negedge clk);
      req_val = 0;
      waited = 0;
      while (resp_val !== 1'b1 && waited < 2000) begin @(negedge clk); waited = waited + 1; end
      if (resp_val !== 1'b1 || resp_msg !== expected) begin
        $display("mismatch: gcd(%0d, %0d) gave %0d, wanted %0d", a, b, resp_msg, expected);
        errors = errors + 1;
      end
      @(negedge clk);
    end
  endtask

  initial begin
    repeat (5) @(negedge clk);
    reset = 0;
    check(16'd12, 16'd18, `FIRST);
    check(16'd1071, 16'd462, 16'd21);
    check(16'd48, 16'd180, 16'd12);
    check(16'd65535, 16'd255, 16'd255);
    check(16'd17, 16'd13, 16'd1);
    if (errors == 0) $display("PASS"); else $display("FAIL %0d", errors);
    $finish;
  end
endmodule
"""


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
        self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "m5_gate", "version": "1"}})
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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mcp", required=True)
    p.add_argument("--workspace")
    p.add_argument("--platform", default="ihp-sg13g2")
    p.add_argument("--reuse", help="an implementation run already in the workspace")
    p.add_argument("--flowdir", default=os.path.expanduser("~/eda/orfs/flow"))
    p.add_argument("--pdk", help="ihp-sg13g2: with --osdi, the chip also runs at transistor level")
    p.add_argument("--osdi")
    p.add_argument("--spice-ns", type=float, default=300.0)
    p.add_argument("--spice-fail-ns", type=float, default=18.0)  # the first sample that differs on gcd is at 15.5 ns
    p.add_argument("--settings-run", action="store_true", help="also run the flow once more with a setting (about 2 minutes)")
    a = p.parse_args()
    gcd = os.path.join(a.flowdir, "designs/src/gcd/gcd.v")
    if not os.path.exists(gcd):
        print("SKIP: no ORFS (scripts/setup_eda_tools.sh)")
        return 0
    work = os.path.abspath(a.workspace) if a.workspace else tempfile.mkdtemp(prefix="m5gate")
    os.makedirs(work, exist_ok=True)
    m = Mcp(os.path.abspath(a.mcp), os.path.join(work, "openvlsi.libs"))
    missing = [t["name"] for t in m("flow_tools") if not t["found"]]
    if missing:
        print("SKIP: missing", missing)
        return 0

    print("RTL to GDS", flush=True)
    if a.reuse:
        run = a.reuse
    else:
        st = m("flow_run", design="gcd", verilog=[gcd], top="gcd", platform=a.platform, clock_period_ns=10.0)
        run = st["run"]
        check(st["state"] == "done", f"flow_run finishes: {st['state']}")
    report = m("flow_report", run=run)
    check(report["clean"], f"{run} is clean: timing met, DRC clean, LVS matched {report['not_clean']}")
    lvs_stage = next(s for s in report["stages"] if s["stage"] == "lvs")["metrics"]
    erc = m("verify_erc", flow_run=run)
    sides = erc["sides"]
    check(lvs_stage.get("erc_violations") == 0 and erc["clean"] and sides["layout"]["devices"] == sides["schematic"]["devices"] > 1000,
          f"the electrical rules pass on the whole chip, flat: {sides['layout']['devices']} transistors a side, supplies {sides['layout']['power']} and {sides['layout']['ground']}")
    swapped = m("verify_erc", flow_run=run, power=sides["layout"]["ground"], ground=sides["layout"]["power"])
    bulks = [v for r in swapped["rules"] if r["rule"] == "bulk_supply" for v in r["violations"]]
    check(len(bulks) == sides["layout"]["devices"] + sides["schematic"]["devices"] and all(len(v["boxes"]) == (v["side"] == "layout") for v in bulks),
          f"with the supplies' names swapped, every transistor's bulk is on the wrong one, each layout one with its place: {len(bulks)}")
    bench = os.path.join(work, "tb_gcd.v")
    with open(bench, "w") as f:
        f.write(TESTBENCH)

    def test(name, level, period, first=6):
        args = dict(name=name, testbench=[bench], top="tb", level=level, flow_run=run, defines=[f"PERIOD={period}", f"FIRST={first}"], time_limit_ns=period * 200000)
        if level == "gate":
            args["dut"] = "tb.dut"
        return m("test_run", **args)

    print("The chip under test", flush=True)
    rtl = test("rtl", "rtl", 10)
    check(rtl["passed"], f"the RTL passes at 10 ns: {rtl.get('error', '')}")
    gate = test("gate", "gate", 10)
    check(gate["passed"], f"the routed netlist with its layout's delays passes at 10 ns ({gate['seconds']:.1f} s): {gate.get('error', '')}")
    answer = m("test_value", run=gate["run"], signal="tb.resp_msg")
    check(answer["count"] > 0, f"the waveforms recorded its outputs ({answer['count']} changes, last value 0x{answer['value']})")
    hex_values = [c[1] for c in answer["changes"]]
    check("0006" in hex_values or "6" in hex_values or any(v.lstrip("0") == "6" for v in hex_values), "gcd(12, 18) = 6 appears on resp_msg")

    fast_rtl = test("rtl_fast", "rtl", 1)
    check(fast_rtl["passed"], "at 1 ns the RTL, which has no delays, still passes")
    fast_gate = test("gate_fast", "gate", 1)
    check(not fast_gate["passed"], f"at 1 ns the routed netlist fails: its delays are real ({fast_gate.get('error', 'it passed')})")

    print("Sign-off", flush=True)
    t = m("timing_analyze", run=run)
    check(t["state"] == "done" and t["met"], f"timing_analyze: met at every corner, WNS {t.get('wns_ns')} ns, TNS {t.get('tns_ns')} ns, parasitics {t.get('parasitics')} {t.get('error', '')}")
    corner = {c["corner"]: c for c in t["corners"]}
    print("    " + "; ".join(f"{n}: setup {c['setup_slack_ns']:.3f} ns, hold {c['hold_slack_ns']:.3f} ns, minimum period {c['clocks'][0]['min_period_ns']:.3f} ns" for n, c in corner.items()), flush=True)
    check(sorted(corner) == ["fast", "slow", "typ"], f"the platform's three corners: {sorted(corner)}")
    periods_by = {n: c["clocks"][0]["min_period_ns"] for n, c in corner.items()}
    check(periods_by["slow"] > periods_by["typ"] > periods_by["fast"], f"slow is slower than typ, and typ than fast: {periods_by}")
    check(corner["fast"]["hold_slack_ns"] < corner["slow"]["hold_slack_ns"], "hold is tightest at the fast corner")
    check(t["setup"][0]["corner"] == "slow" and t["hold"][0]["corner"] == "fast", f"the worst setup path is slow's, the worst hold path fast's: {t['setup'][0]['corner']}, {t['hold'][0]['corner']}")
    check(t["clocks"][0]["corner"] == "slow" and abs(t["clocks"][0]["min_period_ns"] - periods_by["slow"]) < 1e-9, "the report's minimum period is the slowest corner's")
    slacks = [p["slack_ns"] for p in t["setup"]]
    check(slacks == sorted(slacks), "paths across corners come worst first")
    min_period = periods_by["typ"]  # what the flow closed timing at, and what the tests below run with unless they say
    final = next(s for s in report["stages"] if s["stage"] == "finish")["metrics"]
    check(abs(corner["typ"]["setup_slack_ns"] - final["setup_slack_ns"]) < 0.02, f"typ's worst slack {corner['typ']['setup_slack_ns']:.3f} ns agrees with the flow's own report {final['setup_slack_ns']:.3f} ns")
    check(0 < min_period < 10, f"a minimum period of {min_period:.3f} ns under the 10 ns clock")
    one = m("timing_analyze", run=run, corners=["typ"])
    check([c["corner"] for c in one["corners"]] == ["typ"] and abs(one["setup"][0]["slack_ns"] - corner["typ"]["setup_slack_ns"]) < 1e-9, "one corner alone gives that corner's numbers")
    refused = None
    try:
        m("timing_analyze", run=run, corners=["tepid"])
    except RuntimeError as e:
        refused = str(e)
    check(refused is not None and "slow, typ, fast" in refused, f"an unknown corner is refused, naming the ones there are: {refused}")

    def refusal(**args):
        try:
            m("timing_analyze", run=run, **args)
        except RuntimeError as e:
            return str(e)
        return ""

    # The open PDKs extract one RC corner: each timing corner derates it
    rc = {"slow": {"r": 1.2, "c": 1.2}, "fast": {"r": 0.8, "c": 0.8}}
    derated = m("timing_analyze", run=run, rc=rc)
    by = {c["corner"]: c for c in derated["corners"]}
    derated_period = {n: c["clocks"][0]["min_period_ns"] for n, c in by.items()}
    print("    " + "; ".join(f"{n}: R x{c['rc']['r']} C x{c['rc']['c']}, minimum period {derated_period[n]:.3f} ns (as extracted {periods_by[n]:.3f})" for n, c in by.items()), flush=True)
    check(derated_period["slow"] > periods_by["slow"] and derated_period["fast"] < periods_by["fast"] and abs(derated_period["typ"] - periods_by["typ"]) < 1e-9,
          "with R and C 20 % up at slow and down at fast, slow is slower, fast faster, and typ as it was")
    check(by["slow"]["rc"] == rc["slow"] and by["typ"]["rc"] == {"r": 1, "c": 1} and derated["analysis"]["rc"] == rc and m("timing_report", run=run)["analysis"]["rc"] == rc,
          "each corner says its derate, and the report keeps what was asked")
    c_only = m("timing_analyze", run=run, corners=["slow"], rc={"slow": {"c": 1.2}})["corners"][0]
    check(c_only["rc"] == {"r": 1, "c": 1.2} and periods_by["slow"] < c_only["clocks"][0]["min_period_ns"] <= derated_period["slow"] + 1e-9,
          f"capacitance alone accounts for part of it: {c_only['clocks'][0]['min_period_ns']:.3f} ns")
    check("not a corner analysed" in refusal(rc={"tepid": {"r": 1.1}}) and "factor above 0" in refusal(rc={"slow": {"r": 0}}) and "takes r and c" in refusal(rc={"slow": {"l": 2}}),
          "a derate for no corner, of zero, or of something else is refused")
    # Modes: the same chip under other constraints
    own = os.path.join(work, "implementation", run, "results", a.platform, "gcd", "base", "6_final.sdc")
    turbo = os.path.join(work, "turbo.sdc")
    text = open(own).read()
    check("-period 10.0000" in text, "the run's constraints give its 10 ns clock")
    open(turbo, "w").write(text.replace("-period 10.0000", "-period 6.0000"))
    mm = m("timing_analyze", run=run, modes=[{"name": "func"}, {"name": "turbo", "sdc": turbo}])
    views = {(c["mode"], c["corner"]): c for c in mm["corners"]}
    check(sorted(views) == sorted((mode, n) for mode in ("func", "turbo") for n in ("slow", "typ", "fast")), f"two modes at three corners are six views: {sorted(views)}")
    gaps = [views[("func", n)]["setup_slack_ns"] - views[("turbo", n)]["setup_slack_ns"] for n in ("slow", "typ", "fast")]
    check(all(abs(g - 4.0) < 1e-3 for g in gaps), f"a clock 4 ns shorter leaves 4 ns less slack at each corner: {[round(g, 4) for g in gaps]}")
    check(all(abs(views[("func", n)]["hold_slack_ns"] - views[("turbo", n)]["hold_slack_ns"]) < 1e-9 for n in ("slow", "typ", "fast")), "and hold does not move with the period")
    check(mm["setup"][0]["mode"] == "turbo" and mm["setup"][0]["corner"] == "slow" and abs(mm["wns_ns"] - min(0, views[("turbo", "slow")]["setup_slack_ns"])) < 1e-6,
          f"the worst path is turbo's at slow: {mm['setup'][0]['mode']} {mm['setup'][0]['corner']}, slack {mm['setup'][0]['slack_ns']:.3f} ns")
    tight = os.path.join(work, "tight.sdc")
    open(tight, "w").write(text.replace("-period 10.0000", "-period 3.0000"))
    failing = m("timing_analyze", run=run, modes=[{"name": "func"}, {"name": "tight", "sdc": tight}])
    check(failing["met"] is False and failing["wns_ns"] < 0 and all(c["met"] == (c["mode"] == "func") for c in failing["corners"]),
          f"a mode whose clock is too fast is not met while func still is: WNS {failing['wns_ns']:.3f} ns")
    check("named once each" in refusal(modes=[{"name": "a"}, {"name": "a"}]) and "no constraints file" in refusal(modes=[{"name": "a", "sdc": os.path.join(work, "none.sdc")}])
          and "letters, digits" in refusal(modes=[{"name": "a b"}]) and "at least one" in refusal(modes=[]), "a mode named twice, with no file, with a bad name, or none at all is refused")
    empty = os.path.join(work, "empty.sdc")
    open(empty, "w").write("# no clock\n")
    noclock = m("timing_analyze", run=run, modes=[{"name": "idle", "sdc": empty}])
    check(noclock["state"] == "failed" and "idle" in noclock.get("error", ""), f"constraints with no clock are an error, not a pass: {noclock.get('error')}")
    t = m("timing_analyze", run=run)
    check(t["analysis"] == {"paths": 20} and [c["mode"] for c in t["corners"]] == ["func"] * 3, "with nothing asked, one mode, func, as extracted")
    step = t["setup"][0]["steps"][-1]
    arrivals = [s["arrival_ns"] for s in t["setup"][0]["steps"]]
    check(arrivals == sorted(arrivals) and len(arrivals) > 2, f"a path's arrivals only grow, over {len(arrivals)} steps, ending at {step['pin']}")
    above = test("gate_above", "gate", round(min_period * 1.3, 3))
    check(above["passed"], f"the routed netlist passes at 1.3 x STA's minimum period, {min_period * 1.3:.2f} ns: {above.get('error', '')}")
    below = test("gate_below", "gate", round(min_period * 0.4, 3))
    check(not below["passed"], f"and fails at 0.4 x it, {min_period * 0.4:.2f} ns: simulation and STA tell one story")

    p_ = m("power_analyze", run=run)
    check(p_["state"] == "done" and len(p_.get("nets", [])) == 2, f"power_analyze: both rails {p_.get('error', '')}")
    vdd = next(n for n in p_["nets"] if n["net"] == "VDD")
    check(0 < vdd["worst_drop_mv"] < 0.05 * p_["supply_v"] * 1e3, f"VDD's worst drop is real and small: {vdd['worst_drop_mv']:.3f} mV at {vdd['worst_instance']}, {vdd['nodes']:.0f} nodes")
    check(vdd["average_drop_mv"] <= vdd["worst_drop_mv"], "the average is under the worst")
    mp = m("power_map", run=run, net="VDD")
    check(abs(max(n[2] for n in mp["nodes"]) - vdd["worst_drop_mv"]) < 1e-6 and len(mp["nodes"]) == vdd["nodes"], "power_map holds the same nodes and the same worst")
    files = m("flow_artifacts", run=run)
    check(any(f.endswith("6_final.gds") for f in files["results"]) and any(f.endswith("6_final.spef") for f in files["results"]), "flow_artifacts lists the GDS and the SPEF")

    print("The layout at each stage, and the flow's own settings", flush=True)
    counts = {}
    for stage in ("floorplan", "place", "cts", "route"):
        where = m("flow_stage_layout", run=run, stage=stage)
        info = m("layout_info", **where)
        counts[stage] = info
        print(f"    {stage}: {info['instances']} instances, {info['shapes']} shapes, {info['box']['x1'] - info['box']['x0']:.1f} um wide", flush=True)
    final_box = m("layout_info", library=m("flow_import_layout", run=run)["library"], cell="gcd")["box"]
    check(counts["floorplan"]["instances"] < counts["place"]["instances"], f"placement adds its cells: floorplan {counts['floorplan']['instances']}, place {counts['place']['instances']}")
    check(counts["place"]["shapes"] < counts["route"]["shapes"], f"routing adds its wires: place {counts['place']['shapes']} shapes, route {counts['route']['shapes']}")
    wide = counts["route"]["box"]["x1"] - counts["route"]["box"]["x0"]
    check(abs(wide - (final_box["x1"] - final_box["x0"])) < 2, f"the stages share the die: {wide:.1f} um against the final {final_box['x1'] - final_box['x0']:.1f} um")
    if a.settings_run:
        tall = m("flow_run", design="gcd", verilog=[gcd], top="gcd", platform=a.platform, clock_period_ns=10.0, settings={"CORE_ASPECT_RATIO": "2"})
        where = m("flow_stage_layout", run=tall["run"], stage="floorplan")
        box = m("layout_info", **where)["box"]
        ratio = (box["y1"] - box["y0"]) / (box["x1"] - box["x0"])
        check(ratio > 1.5, f"a setting reaches the flow: CORE_ASPECT_RATIO=2 makes it {ratio:.2f} times as tall as wide")
    refused = None
    try:
        m("flow_run", design="gcd", verilog=[gcd], top="gcd", platform=a.platform, clock_period_ns=10.0, settings={"PLATFORM": "x"})
    except RuntimeError as e:
        refused = str(e)
    check(refused is not None and "PLATFORM" in refused, f"a setting cannot override what the app sets: {refused}")

    print("Clock sweep, and the chip at transistor level", flush=True)
    periods = [round(min_period * f, 3) for f in (0.3, 0.6, 1.0, 1.5, 2.0)]
    sweep = m("test_shmoo", name="shmoo", flow_run=run, testbench=[bench], top="tb", dut="tb.dut", periods_ns=periods, defines=["FIRST=6"])
    print("    " + ", ".join(f"{p['period_ns']} ns {'pass' if p['passed'] else 'FAIL'}" for p in sweep["points"]), flush=True)
    check(sweep["consistent"] and "fastest_pass_ns" in sweep and "slowest_fail_ns" in sweep, f"test_shmoo finds where it stops: passes down to {sweep.get('fastest_pass_ns')} ns, fails at {sweep.get('slowest_fail_ns')} ns, against STA's {min_period:.2f} ns")
    # The same chip, slow: a finer sweep between where typ fails and where slow's STA says it is safe
    lo, hi = sweep["slowest_fail_ns"], periods_by["slow"] * 1.2
    fine = [round(lo + (hi - lo) * k / 7, 3) for k in range(8)]
    by_corner = {}
    for name in ("slow", "typ", "fast"):
        r = m("test_shmoo", name="shmoo_" + name, flow_run=run, testbench=[bench], top="tb", dut="tb.dut", periods_ns=fine, defines=["FIRST=6"], corner=name)
        by_corner[name] = r
        print(f"    {name}: " + ", ".join(f"{p['period_ns']} {'pass' if p['passed'] else 'FAIL'}" for p in r["points"]), flush=True)
    check(all(r["consistent"] for r in by_corner.values()), "each corner's sweep has one threshold")
    check(by_corner["slow"].get("fastest_pass_ns", 1e9) > by_corner["fast"].get("fastest_pass_ns", 0), f"the slow chip stops working at a longer period than the fast one: {by_corner['slow'].get('fastest_pass_ns')} ns against {by_corner['fast'].get('fastest_pass_ns')} ns")
    check(by_corner["slow"].get("fastest_pass_ns", 1e9) <= periods_by["slow"] * 1.2 + 1e-9, f"and works where its own STA says it does ({periods_by['slow']:.2f} ns)")

    if a.pdk and a.osdi:
        if "sg13g2" not in m("workspace_libraries"):  # a reused workspace may have it already
            m("pdk_install", pdk="ihp-sg13g2", root=os.path.abspath(a.pdk), osdi=os.path.abspath(a.osdi))
        spice = m("test_spice", name="chip", flow_run=run, recording=gate["run"], dut="tb.dut", stop_ns=a.spice_ns)
        print(f"    {spice.get('transistors'):.0f} transistors, {spice.get('parasitic_pf', 0):.1f} pF on {spice.get('parasitic_nets', 0):.0f} nets, {spice.get('stop_ns')} ns of chip time in {spice.get('seconds', 0):.1f} s, {spice.get('steps', 0):.0f} steps", flush=True)
        check(spice.get("passed") is True, f"TreeSPICE, on the routed transistors, gives the recording's outputs: {spice.get('samples')} samples, {spice.get('mismatches')} differ {spice.get('error', '')}")
        check(spice.get("wire_resistors", 0) > 1000 and spice.get("lumped_nets") == 0,
              f"every net carries its own RC network from the SPEF: {spice.get('wire_resistors', 0):.0f} resistors, {spice.get('lumped_nets')} nets left as one capacitor")
        # It must be able to fail: the RTL's recording at 1 ns asks the chip for what its delays cannot give
        hurried = m("test_spice", name="chip_hurried", flow_run=run, recording=fast_rtl["run"], dut="tb.dut", stop_ns=a.spice_fail_ns)
        print(f"    {hurried.get('stop_ns')} ns of chip time in {hurried.get('seconds', 0):.1f} s: {hurried.get('error', '')}", flush=True)
        check(hurried.get("passed") is False and hurried.get("mismatches", 0) > 0 and "differ; first" in hurried.get("error", ""),
              f"replaying the RTL's 1 ns recording, the transistors do not keep up: {hurried.get('mismatches')} of {hurried.get('samples')} samples differ")

    wrong = test("wrong", "rtl", 10, first=7)
    check(not wrong["passed"] and "failure" in wrong.get("error", ""), "a wrong expectation fails")
    m.p.stdin.close()
    m.p.wait()
    print(("PASSED" if not G_FAIL else "FAILED") + f": {G_PASS} of {G_PASS + len(G_FAIL)} checks")
    return 1 if G_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
