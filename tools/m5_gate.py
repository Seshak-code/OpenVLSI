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
    check(t["state"] == "done" and t["met"], f"timing_analyze: met, WNS {t.get('wns_ns')} ns, TNS {t.get('tns_ns')} ns, parasitics {t.get('parasitics')} {t.get('error', '')}")
    min_period = t["clocks"][0]["min_period_ns"]
    worst_slack = t["setup"][0]["slack_ns"]
    final = next(s for s in report["stages"] if s["stage"] == "finish")["metrics"]
    check(abs(worst_slack - final["setup_slack_ns"]) < 0.02, f"its worst slack {worst_slack:.3f} ns agrees with the flow's own report {final['setup_slack_ns']:.3f} ns")
    check(0 < min_period < 10, f"a minimum period of {min_period:.3f} ns under the 10 ns clock")
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

    print("Clock sweep, and the chip at transistor level", flush=True)
    periods = [round(min_period * f, 3) for f in (0.3, 0.6, 1.0, 1.5, 2.0)]
    sweep = m("test_shmoo", name="shmoo", flow_run=run, testbench=[bench], top="tb", dut="tb.dut", periods_ns=periods, defines=["FIRST=6"])
    print("    " + ", ".join(f"{p['period_ns']} ns {'pass' if p['passed'] else 'FAIL'}" for p in sweep["points"]), flush=True)
    check(sweep["consistent"] and "fastest_pass_ns" in sweep and "slowest_fail_ns" in sweep, f"test_shmoo finds where it stops: passes down to {sweep.get('fastest_pass_ns')} ns, fails at {sweep.get('slowest_fail_ns')} ns, against STA's {min_period:.2f} ns")

    if a.pdk and a.osdi:
        m("pdk_install", pdk="ihp-sg13g2", root=os.path.abspath(a.pdk), osdi=os.path.abspath(a.osdi))
        spice = m("test_spice", name="chip", flow_run=run, recording=gate["run"], dut="tb.dut", stop_ns=a.spice_ns)
        print(f"    {spice.get('transistors'):.0f} transistors, {spice.get('parasitic_pf', 0):.1f} pF on {spice.get('parasitic_nets', 0):.0f} nets, {spice.get('stop_ns')} ns of chip time in {spice.get('seconds', 0):.1f} s, {spice.get('steps', 0):.0f} steps", flush=True)
        check(spice.get("passed") is True, f"TreeSPICE, on the routed transistors, gives the recording's outputs: {spice.get('samples')} samples, {spice.get('mismatches')} differ {spice.get('error', '')}")

    wrong = test("wrong", "rtl", 10, first=7)
    check(not wrong["passed"] and "failure" in wrong.get("error", ""), "a wrong expectation fails")
    m.p.stdin.close()
    m.p.wait()
    print(("PASSED" if not G_FAIL else "FAILED") + f": {G_PASS} of {G_PASS + len(G_FAIL)} checks")
    return 1 if G_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
