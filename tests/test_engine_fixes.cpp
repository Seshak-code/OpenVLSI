// test_engine_fixes.cpp — Regression checks for engine review fixes 1-4.
// Drives the public ISimulationEngine API (both CPU and default/GPU modes):
//   1. NaN results must NOT report converged (fail-closed convergence)
//   2. Transient must terminate and match the analytic RC answer
//   3. Parsed diodes get SPICE defaults and honour a trailing .model card
//   4. Inductors are shorts at DC
// Run: ./build/test_engine_fixes

#include "engine_api/isimulation_engine.h"
#include "engine_api/simulation_session.h"
#include "engine_api/simulation_dto.h"
#include "engine_api/engine_callbacks.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace acutesim;
using namespace acutesim::compute::orchestration;

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

static SimulationResponseDTO run(PrecisionMode mode, const std::string& netlist,
                                 double tranStep = 0, double tranStop = 0) {
    auto engine = ISimulationEngine::create();
    engine->setPrecisionMode(mode);
    auto session = engine->createSession();
    SimulationRequestDTO req;
    req.source.payload = netlist;
    EngineCallbacks cb;
    if (tranStop > 0) {
        req.transient.stepSizeS = tranStep;
        req.transient.stopTimeS = tranStop;
        return session->runTransient(req, cb, nullptr);
    }
    return session->runDC(req, cb);
}

// V(node) of the last waveform point; node numbers are 1-based.
static double V(const SimulationResponseDTO& r, int node) {
    if (r.waveformPoints.empty() || (int)r.waveformPoints.back().nodeVoltages.size() < node)
        return NAN;
    return r.waveformPoints.back().nodeVoltages[node - 1];
}

static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

int main() {
    for (PrecisionMode mode : {PrecisionMode::Native_F64, PrecisionMode::Mixed}) {
        std::printf("\n== mode %s ==\n", mode == PrecisionMode::Native_F64 ? "CPU" : "default (GPU if available)");

        // Fix 4: inductor is a short at DC (1 mOhm model -> ~5 uV error)
        auto r = run(mode, "* series RL\nV1 1 0 DC 5\nL1 1 2 1m\nR1 2 0 1k\n.op\n");
        check(r.converged && near(V(r, 2), 5.0, 1e-4), "series RL: V(2) = 5 V at DC");
        r = run(mode, "* shunt RL\nV1 1 0 DC 5\nR1 1 2 1k\nL1 2 0 1m\n.op\n");
        check(r.converged && near(V(r, 2), 0.0, 1e-4), "shunt RL: V(2) = 0 V at DC");

        // Fix 3: default diode (IS=1e-14) and trailing .model card (IS=1e-12)
        r = run(mode, "* diode default\nV1 1 0 DC 5\nR1 1 2 1k\nD1 2 0 NOCARD\n.op\n");
        double vd = V(r, 2), id = (5.0 - vd) / 1e3;
        check(r.converged && near(vd, 0.02585 * std::log(id / 1e-14 + 1), 1e-4),
              "diode with SPICE defaults matches Shockley (V=" + std::to_string(vd) + ")");
        r = run(mode, "* diode card after\nV1 1 0 DC 5\nR1 1 2 1k\nD1 2 0 DB\n.model DB D(IS=1e-12)\n.op\n");
        vd = V(r, 2); id = (5.0 - vd) / 1e3;
        check(r.converged && near(vd, 0.02585 * std::log(id / 1e-12 + 1), 1e-4),
              "trailing .model IS=1e-12 is applied (V=" + std::to_string(vd) + ")");

        // Junction exponent cap: at 1 A (Is=1e-14) the old cap of 30 made the
        // diode linear above 0.107 A and gave 0.9915 V; Shockley gives 0.8335 V.
        r = run(mode, "* 1A diode\nI1 0 1 DC 1\nD1 1 0 DM\n.model DM D(IS=1e-14)\n.op\n");
        check(r.converged && near(V(r, 1), 0.02585 * std::log(1.0 / 1e-14 + 1), 2e-3),
              "1 A diode follows Shockley, not the old linear clamp (V=" + std::to_string(V(r, 1)) + ")");

        // Fix 1: a NaN solution must not be reported as converged (N=0 -> /0)
        r = run(mode, "* nan diode\nV1 1 0 DC 5\nR1 1 2 1k\nD1 2 0 DZ\n.model DZ D(N=0)\n.op\n");
        check(!r.converged, "NaN-producing circuit reports NOT converged");

        // GPU acceptance: a device the GPU loop doesn't stamp (E source) must
        // not yield a wrong answer flagged converged. Right answer or honest
        // failure both pass, so this survives VCVS support being added later.
        r = run(mode, "* noninverting opamp\nV1 1 0 DC 5\nRB 1 0 10k\nE1 2 0 1 3 999k\n"
                      "R1 2 3 20k\nR2 3 0 10k\n.op\n");
        check(!r.converged || near(V(r, 2), 15.0, 1e-2),
              "E-source op-amp: no false convergence (converged=" +
              std::to_string(r.converged) + ", V(out)=" + std::to_string(V(r, 2)) + ")");

        // E/G/J on the default path (were silently dropped). Gain 999k also
        // exercises the scale-aware KCL test (raw residual ~1e-5 A from rounding).
        r = run(mode, "* noninverting opamp\nV1 1 0 DC 5\nRB 1 0 10k\nE1 2 0 1 3 999k\n"
                      "R1 2 3 20k\nR2 3 0 10k\n.op\n");
        check(r.converged && near(V(r, 2), 15.0, 1e-3),
              "E-source op-amp converges to 15 V (V=" + std::to_string(V(r, 2)) + ")");
        r = run(mode, "* G source\nV1 1 0 DC 2\nRB 1 0 1k\nG1 2 0 1 0 1m\nR2 2 0 1k\n.op\n");
        check(r.converged && near(V(r, 2), -2.0, 1e-3), "VCCS: 1 mS * 2 V into 1k gives -2 V");
        // Shichman-Hodges sat: Id = BETA*(Vgs-VTO)^2 = 1e-3*(−1+2)^2 = 1 mA -> V(d) = 10 - 1 = 9 V
        r = run(mode, "* njf\nVDD 1 0 DC 10\nVG 2 0 DC -1\nRD 1 3 1k\nJ1 3 2 0 JN\n"
                      ".model JN NJF(BETA=1e-3 VTO=-2)\n.op\n");
        check(r.converged && near(V(r, 3), 9.0, 1e-3), "N-JFET saturation current matches hand calc");
        r = run(mode, "* pjf mirror\nVDD 1 0 DC -10\nVG 2 0 DC 1\nRD 1 3 1k\nJ1 3 2 0 JP\n"
                      ".model JP PJF(BETA=1e-3 VTO=-2)\n.op\n");
        check(r.converged && near(V(r, 3), -9.0, 1e-3), "P-JFET is the exact mirror (was mis-linearised)");

        // UIC: op-amp integrator has no DC operating point (open loop); SPICE
        // skips it with UIC. Also drives the Trap->Gear2 switch, whose SoA stamp
        // used to be Backward Euler and made every later step fail.
        r = run(mode, "* integrator\nVIN 1 0 SIN(0 15 60)\nR1 1 2 10k\nC1 2 3 150u\n"
                      "E1 3 0 0 2 999k\n.tran 1e-4 30e-3 uic\n", 1e-4, 30e-3);
        {
            double w = 2 * 3.141592653589793 * 60, t = 30e-3;
            double ideal = -(15.0 / (1.5 * w)) * (1 - std::cos(w * t));
            check(r.converged && near(V(r, 3), ideal, 1e-4),
                  "UIC integrator runs to 30 ms and matches -(1/RC)*integral (V=" +
                  std::to_string(V(r, 3)) + ", ideal " + std::to_string(ideal) + ")");
        }

        // Fix 2: RC transient at dt = tau/100 terminates and is accurate
        r = run(mode, "* rc\nV1 1 0 DC 5\nR1 1 2 1k\nC1 2 0 1u\n.tran 1e-5 1e-3\n", 1e-5, 1e-3);
        double expect = 5.0 * (1.0 - std::exp(-1.0));
        check(r.converged && near(V(r, 2), expect, 1e-3) &&
              near(r.waveformPoints.back().timeS, 1e-3, 1e-12),
              "RC transient reaches t=1ms with V(2)=" + std::to_string(V(r, 2)) +
              " (analytic " + std::to_string(expect) + ")");

        // Waveform sources on the default path (were stuck at 0 V / DC)
        r = run(mode, "* sin\nV1 1 0 SIN(0 1 1k)\nR1 1 0 1k\n", 1e-5, 2.5e-4);
        check(r.converged && near(V(r, 1), 1.0, 2e-3),
              "SIN source peaks at 1 V at t=T/4 (V=" + std::to_string(V(r, 1)) + ")");
        r = run(mode, "* pulse rc\nV1 1 0 PULSE(0 5 0 1n 1n 1 2)\nR1 1 2 1k\nC1 2 0 1u\n", 1e-5, 1e-3);
        check(r.converged && near(V(r, 2), expect, 2e-3),
              "PULSE-driven RC matches analytic (V=" + std::to_string(V(r, 2)) + ")");

        // Fix 5: parser
        r = run(mode, "* ac then sin\nV1 1 0 AC 1 SIN(0 1 1k)\nR1 1 0 1k\n.op\n");
        check(r.converged && near(V(r, 1), 0.0, 1e-4),
              "AC + SIN source: DC value is SIN's t=0 value, not 1000 V");
        r = run(mode, "* param\n.param RV=1k\nV1 1 0 DC 5\nR1 1 2 {RV}\nR2 2 0 1k\n.op\n");
        check(r.converged && near(V(r, 2), 2.5, 1e-3), ".param {RV} is substituted");
        r = run(mode, "* late pnp model\nVCC 1 0 DC 5\nRE 1 2 1k\nVB 3 0 DC 3\nQ1 4 3 2 QP\n"
                      "RC 4 0 1k\n.model QP PNP(IS=1e-15 BF=100)\n.op\n");
        // Hand: Ie~(5-Ve)/1k, Ic=Ie*100/101, Vbe=0.02585*ln(Ic/1e-15)=0.7205 -> Ve=3.7205
        // (an NPN would not conduct at all; ignoring IS from the card gives 3.662)
        check(r.converged && near(V(r, 2), 3.7205, 2e-3),
              ".model after the device still makes it a PNP (V(e)=" + std::to_string(V(r, 2)) + ")");
        r = run(mode, "* unknown\nV1 1 0 5\nZ1 1 0 5\n.op\n");
        check(!r.converged && r.errorDetail.find("unknown element") != std::string::npos,
              "unknown element is a parse error: " + r.errorDetail);
        r = run(mode, "* cccs\nV1 1 0 1\nR1 1 0 1k\nF1 0 2 V1 10\nR2 2 0 1k\n.op\n");
        check(!r.converged && r.errorDetail.find("not supported") != std::string::npos,
              "unsupported F source is an error, not a silent no-op");

        // Fix 2: invalid timestep is an error, not an infinite loop
        r = run(mode, "* rc\nV1 1 0 DC 5\nR1 1 2 1k\nC1 2 0 1u\n", 0.0, 1e-3);
        check(!r.converged && !r.errorDetail.empty(), "zero timestep returns an error");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
