/**
 * test_ad_integration.cpp
 *
 * Validates that Dual<double> automatic differentiation produces derivatives
 * matching finite-difference approximation for all device models.
 *
 * Tolerance: AD must match finite-diff within 1e-4 relative error.
 */

#include <iostream>
#include <cmath>
#include <cassert>
#include <string>
#include "acutesim_engine/autodiff/dual.h"
#include "acutesim_engine/physics/device_physics.h"

static int tests_passed = 0;
static int tests_failed = 0;

static bool check_derivative(const std::string& label,
                             double ad_grad, double fd_grad,
                             double rel_tol = 1e-4) {
    double denom = std::max(std::abs(fd_grad), 1e-15);
    double rel_err = std::abs(ad_grad - fd_grad) / denom;

    bool pass = rel_err < rel_tol;
    if (!pass) {
        // Also accept if both are near zero
        if (std::abs(ad_grad) < 1e-9 && std::abs(fd_grad) < 1e-9) {
            pass = true;
        }
    }

    if (pass) {
        tests_passed++;
        std::cout << "  [PASS] " << label
                  << " AD=" << ad_grad << " FD=" << fd_grad
                  << " rel_err=" << rel_err << "\n";
    } else {
        tests_failed++;
        std::cout << "  [FAIL] " << label
                  << " AD=" << ad_grad << " FD=" << fd_grad
                  << " rel_err=" << rel_err << "\n";
    }
    return pass;
}

// ===========================================================================
// Diode Tests
// ===========================================================================
void test_diode() {
    std::cout << "\n[Diode AD Test]\n";

    DiodeParams<double> p;
    p.Is = 1e-14;
    p.N = 1.0;
    p.Vt = 0.02585;
    p.Rs = 0.0;

    double delta = 1e-7;

    // Forward bias: vd = 0.65V
    {
        double vd = 0.65;
        Dual<double> vd_dual(vd, 1.0);
        Dual<double> i_ad = diode_current_clamped(vd_dual, p);

        double i_plus = diode_current_clamped(vd + delta, p);
        double i_minus = diode_current_clamped(vd - delta, p);
        double fd_grad = (i_plus - i_minus) / (2.0 * delta);

        check_derivative("Diode forward (vd=0.65V)", i_ad.grad, fd_grad);
    }

    // Reverse bias: vd = -0.3V
    {
        double vd = -0.3;
        Dual<double> vd_dual(vd, 1.0);
        Dual<double> i_ad = diode_current_clamped(vd_dual, p);

        double i_plus = diode_current_clamped(vd + delta, p);
        double i_minus = diode_current_clamped(vd - delta, p);
        double fd_grad = (i_plus - i_minus) / (2.0 * delta);

        check_derivative("Diode reverse (vd=-0.3V)", i_ad.grad, fd_grad);
    }
}

// ===========================================================================
// MOSFET Tests
// ===========================================================================
void test_mosfet() {
    std::cout << "\n[MOSFET AD Test]\n";

    // mos1_currents(): drain current's Dual derivatives vs central differences,
    // in saturation, linear, reverse (Vds < 0: source/drain exchange) and with
    // the body effect (Vbs < 0), for gate, drain and bulk.
    Mos1Params<double> p;
    p.beta = 200e-6 * 10.0;   // KP * W/L
    p.vto = 0.7; p.lambda = 0.02; p.gamma = 0.5; p.phi = 0.7;
    const double delta = 1e-7;
    struct Point { const char* name; double vd, vg, vs, vb; };
    const Point pts[] = {{"saturation", 2.0, 1.5, 0.0, 0.0}, {"linear", 0.3, 1.5, 0.0, 0.0},
                         {"reverse Vds<0", -0.4, 1.5, 0.0, 0.0}, {"body Vbs=-1", 2.0, 1.8, 0.0, -1.0}};
    for (const auto& q : pts) {
        double v[4] = {q.vd, q.vg, q.vs, q.vb};
        auto id = [&](const double* x) { return mos1_currents(x[0], x[1], x[2], x[3], p).d; };
        for (int u : {1, 0, 3}) {  // d/dVg (gm), d/dVd (gds), d/dVb (gmbs)
            Dual<double> x[4];
            for (int k = 0; k < 4; ++k) x[k] = Dual<double>(v[k], k == u ? 1.0 : 0.0);
            const double ad = mos1_currents(x[0], x[1], x[2], x[3], p).d.grad;
            double hi[4], lo[4];
            for (int k = 0; k < 4; ++k) { hi[k] = lo[k] = v[k]; }
            hi[u] += delta; lo[u] -= delta;
            const double fd = (id(hi) - id(lo)) / (2.0 * delta);
            check_derivative(std::string("MOSFET ") + q.name + (u == 1 ? " gm" : u == 0 ? " gds" : " gmbs"), ad, fd);
        }
    }
}

// ===========================================================================
// JFET Tests
// ===========================================================================
void test_jfet() {
    std::cout << "\n[JFET AD Test]\n";

    JfetModel<double> p;  // ngspice level 1; vgd = vgs - vds
    p.beta = 1e-4;
    p.vto = -2.0;
    p.lambda = 0.01;
    p.is = 1e-14;
    p.vt = 0.02585;
    auto jfet_ids = [&](auto vgs, auto vds) { return jfet_eval(vgs, vgs - vds, p).id; };

    double delta = 1e-7;

    // Saturation: vgs=0.0 (N-ch, Vto=-2), vds=5.0
    {
        double vgs = 0.0, vds = 5.0;

        Dual<double> ids_vgs = jfet_ids(Dual<double>(vgs, 1.0), Dual<double>(vds, 0.0));
        double gm_ad = ids_vgs.grad;

        double i_p = jfet_ids(vgs + delta, vds);
        double i_m = jfet_ids(vgs - delta, vds);
        double gm_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET saturation gm (vgs=0, vds=5)", gm_ad, gm_fd);

        Dual<double> ids_vds = jfet_ids(Dual<double>(vgs, 0.0), Dual<double>(vds, 1.0));
        double gds_ad = ids_vds.grad;

        i_p = jfet_ids(vgs, vds + delta);
        i_m = jfet_ids(vgs, vds - delta);
        double gds_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET saturation gds (vgs=0, vds=5)", gds_ad, gds_fd);
    }

    // Linear: vgs=-0.5, vds=0.5
    {
        double vgs = -0.5, vds = 0.5;

        Dual<double> ids_vgs = jfet_ids(Dual<double>(vgs, 1.0), Dual<double>(vds, 0.0));
        double gm_ad = ids_vgs.grad;

        double i_p = jfet_ids(vgs + delta, vds);
        double i_m = jfet_ids(vgs - delta, vds);
        double gm_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET linear gm (vgs=-0.5, vds=0.5)", gm_ad, gm_fd);

        Dual<double> ids_vds = jfet_ids(Dual<double>(vgs, 0.0), Dual<double>(vds, 1.0));
        double gds_ad = ids_vds.grad;

        i_p = jfet_ids(vgs, vds + delta);
        i_m = jfet_ids(vgs, vds - delta);
        double gds_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET linear gds (vgs=-0.5, vds=0.5)", gds_ad, gds_fd);
    }
}

// ===========================================================================
// BJT Gummel-Poon: Dual derivatives of bjt_gp (what the engine stamps) vs FD
// ===========================================================================
void test_bjt() {
    std::cout << "\n[BJT Gummel-Poon AD Test]\n";
    const double vt = 0.02585;
    BjtModel<double> p;
    p.is = 1e-15; p.bf = 150; p.br = 3; p.vtf = 1.02 * vt; p.vtr = 1.01 * vt;
    p.invVaf = 1.0 / 40; p.invVar = 1.0 / 15; p.oikf = 1.0 / 20e-3; p.oikr = 1.0 / 5e-3;
    p.ise = 5e-14; p.vte = 1.6 * vt; p.isc = 1e-13; p.vtc = 1.9 * vt;
    p.cje = 2e-12; p.cjcInt = 1e-12; p.tf = 400e-12; p.xtf = 3; p.ovtf = 1.0 / (1.44 * 4); p.xjtf = 10e-3; p.tr = 20e-9;
    const double delta = 1e-7;
    const char* region[] = {"forward active", "saturation", "reverse"};
    const double bias[3][2] = {{0.7, -4.3}, {0.75, 0.5}, {-0.3, 0.6}};
    for (int r = 0; r < 3; ++r)
        for (int w = 0; w < 2; ++w) {  // w = 0: d/dVbe, 1: d/dVbc
            const double vbe = bias[r][0], vbc = bias[r][1];
            const auto ad = bjt_gp(Dual<double>(vbe, w == 0 ? 1.0 : 0.0), Dual<double>(vbc, w == 1 ? 1.0 : 0.0),
                                   Dual<double>(vbc, w == 1 ? 1.0 : 0.0), Dual<double>(0.0), p);
            const double dp = w == 0 ? delta : 0.0, dc = w == 1 ? delta : 0.0;
            const auto pl = bjt_gp(vbe + dp, vbc + dc, vbc + dc, 0.0, p);
            const auto mi = bjt_gp(vbe - dp, vbc - dc, vbc - dc, 0.0, p);
            const std::string at = std::string(region[r]) + (w == 0 ? " d/dVbe " : " d/dVbc ");
            check_derivative("BJT GP " + at + "Ic", ad.cc.grad, (pl.cc - mi.cc) / (2 * delta));
            check_derivative("BJT GP " + at + "Ib", ad.cb.grad, (pl.cb - mi.cb) / (2 * delta));
            check_derivative("BJT GP " + at + "Qbe", ad.q[0].grad, (pl.q[0] - mi.q[0]) / (2 * delta));
            check_derivative("BJT GP " + at + "Qbc", ad.q[1].grad, (pl.q[1] - mi.q[1]) / (2 * delta));
        }
    // SPICE diode (diode_iq): forward, cubic reverse tail, breakdown past -BV; current and charge
    DiodeModel<double> d;
    d.is = 1e-14; d.vte = 1.3 * vt; d.cjo = 5e-12; d.vj = 0.7; d.m = 0.45; d.tt = 20e-9;
    d.breakdown = true; d.xbv = diode_breakdown_xbv(5.1, 5e-3, d.is, d.vte);
    for (double vd : {0.65, -0.5, -5.2}) {
        const auto ad = diode_iq(Dual<double>(vd, 1.0), d);
        const auto pl = diode_iq(vd + delta, d), mi = diode_iq(vd - delta, d);
        check_derivative("Diode vd=" + std::to_string(vd) + " I", ad.first.grad, (pl.first - mi.first) / (2 * delta));
        check_derivative("Diode vd=" + std::to_string(vd) + " Q", ad.second.grad, (pl.second - mi.second) / (2 * delta));
    }
}

// ===========================================================================
int main() {
    std::cout << "=== AD Integration Test Suite ===\n";

    test_diode();
    test_mosfet();
    test_jfet();
    test_bjt();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n";

    if (tests_failed > 0) {
        std::cerr << "AD INTEGRATION TEST FAILED\n";
        return 1;
    }

    std::cout << "ALL AD INTEGRATION TESTS PASSED\n";
    return 0;
}
