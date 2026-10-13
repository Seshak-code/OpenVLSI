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
    p.Tnom = 300.15;
    p.Temp = 300.15;
    p.Eg = 1.11;
    p.Xti = 3.0;

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

    JFETParams<double> p;
    p.Beta = 1e-4;
    p.Vto = -2.0;
    p.Lambda = 0.01;
    p.Is = 1e-14;
    p.N = 1.0;
    p.Vt = 0.02585;

    double delta = 1e-7;

    // Saturation: vgs=0.0 (N-ch, Vto=-2), vds=5.0
    {
        double vgs = 0.0, vds = 5.0;

        Dual<double> ids_vgs = jfet_ids(Dual<double>(vgs, 1.0), Dual<double>(vds, 0.0), p);
        double gm_ad = ids_vgs.grad;

        double i_p = jfet_ids(vgs + delta, vds, p);
        double i_m = jfet_ids(vgs - delta, vds, p);
        double gm_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET saturation gm (vgs=0, vds=5)", gm_ad, gm_fd);

        Dual<double> ids_vds = jfet_ids(Dual<double>(vgs, 0.0), Dual<double>(vds, 1.0), p);
        double gds_ad = ids_vds.grad;

        i_p = jfet_ids(vgs, vds + delta, p);
        i_m = jfet_ids(vgs, vds - delta, p);
        double gds_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET saturation gds (vgs=0, vds=5)", gds_ad, gds_fd);
    }

    // Linear: vgs=-0.5, vds=0.5
    {
        double vgs = -0.5, vds = 0.5;

        Dual<double> ids_vgs = jfet_ids(Dual<double>(vgs, 1.0), Dual<double>(vds, 0.0), p);
        double gm_ad = ids_vgs.grad;

        double i_p = jfet_ids(vgs + delta, vds, p);
        double i_m = jfet_ids(vgs - delta, vds, p);
        double gm_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET linear gm (vgs=-0.5, vds=0.5)", gm_ad, gm_fd);

        Dual<double> ids_vds = jfet_ids(Dual<double>(vgs, 0.0), Dual<double>(vds, 1.0), p);
        double gds_ad = ids_vds.grad;

        i_p = jfet_ids(vgs, vds + delta, p);
        i_m = jfet_ids(vgs, vds - delta, p);
        double gds_fd = (i_p - i_m) / (2.0 * delta);

        check_derivative("JFET linear gds (vgs=-0.5, vds=0.5)", gds_ad, gds_fd);
    }
}

// ===========================================================================
// Zener Diode Tests
// ===========================================================================
void test_zener() {
    std::cout << "\n[Zener Diode AD Test]\n";

    ZenerParams<double> p;
    p.BV = 5.1;
    p.IBV = 0.049;
    p.Rs = 7.0;
    p.N = 1.0;
    p.Is = 1e-14;
    p.Vt = 0.02585;

    double delta = 1e-7;

    // Forward bias: vd = 0.65V
    {
        double vd = 0.65;
        Dual<double> vd_dual(vd, 1.0);
        Dual<double> i_ad = zener_current(vd_dual, p);

        double i_p = zener_current(vd + delta, p);
        double i_m = zener_current(vd - delta, p);
        double fd_grad = (i_p - i_m) / (2.0 * delta);

        check_derivative("Zener forward (vd=0.65V)", i_ad.grad, fd_grad);
    }

    // Reverse (not in breakdown): vd = -2.0V
    {
        double vd = -2.0;
        Dual<double> vd_dual(vd, 1.0);
        Dual<double> i_ad = zener_current(vd_dual, p);

        double i_p = zener_current(vd + delta, p);
        double i_m = zener_current(vd - delta, p);
        double fd_grad = (i_p - i_m) / (2.0 * delta);

        check_derivative("Zener reverse (vd=-2.0V)", i_ad.grad, fd_grad);
    }

    // Breakdown region: vd = -5.2V (just past -BV)
    {
        double vd = -5.2;
        Dual<double> vd_dual(vd, 1.0);
        Dual<double> i_ad = zener_current(vd_dual, p);

        double i_p = zener_current(vd + delta, p);
        double i_m = zener_current(vd - delta, p);
        double fd_grad = (i_p - i_m) / (2.0 * delta);

        check_derivative("Zener breakdown (vd=-5.2V)", i_ad.grad, fd_grad);
    }
}

// ===========================================================================
// BJT Ebers-Moll Tests (verify manual derivatives match Dual<double>)
// ===========================================================================
void test_bjt() {
    std::cout << "\n[BJT Ebers-Moll AD Test]\n";

    BJTParams<double> p;
    p.Is = 1e-14;
    p.BetaF = 100.0;
    p.BetaR = 1.0;
    p.Vt = 0.02585;

    double delta = 1e-7;

    // NPN active: Vc=5.0, Vb=0.7, Ve=0.0
    {
        double vc = 5.0, vb = 0.7, ve = 0.0;

        BJTCurrents<double> res = bjt_ebers_moll(vc, vb, ve, true, p);

        // FD for Ic wrt Vb
        auto res_p = bjt_ebers_moll(vc, vb + delta, ve, true, p);
        auto res_m = bjt_ebers_moll(vc, vb - delta, ve, true, p);
        double dIc_dVb_fd = (res_p.Ic - res_m.Ic) / (2.0 * delta);

        // g_cb = dIc/dVb + dIc/dVc (combined), but from the code:
        // g_cb = dIc_dVbe + dIc_dVbc  (where Vbe = Vb - Ve, Vbc = Vb - Vc)
        // FD for Ic wrt Vc
        res_p = bjt_ebers_moll(vc + delta, vb, ve, true, p);
        res_m = bjt_ebers_moll(vc - delta, vb, ve, true, p);
        double dIc_dVc_fd = (res_p.Ic - res_m.Ic) / (2.0 * delta);

        // FD for Ic wrt Ve
        res_p = bjt_ebers_moll(vc, vb, ve + delta, true, p);
        res_m = bjt_ebers_moll(vc, vb, ve - delta, true, p);
        double dIc_dVe_fd = (res_p.Ic - res_m.Ic) / (2.0 * delta);

        check_derivative("BJT NPN g_cc (dIc/dVc)", res.g_cc, dIc_dVc_fd);
        check_derivative("BJT NPN g_cb (dIc/dVb)", res.g_cb, dIc_dVb_fd);
        check_derivative("BJT NPN g_ce (dIc/dVe)", res.g_ce, dIc_dVe_fd);

        // FD for Ib
        res_p = bjt_ebers_moll(vc, vb + delta, ve, true, p);
        res_m = bjt_ebers_moll(vc, vb - delta, ve, true, p);
        double dIb_dVb_fd = (res_p.Ib - res_m.Ib) / (2.0 * delta);

        res_p = bjt_ebers_moll(vc + delta, vb, ve, true, p);
        res_m = bjt_ebers_moll(vc - delta, vb, ve, true, p);
        double dIb_dVc_fd = (res_p.Ib - res_m.Ib) / (2.0 * delta);

        res_p = bjt_ebers_moll(vc, vb, ve + delta, true, p);
        res_m = bjt_ebers_moll(vc, vb, ve - delta, true, p);
        double dIb_dVe_fd = (res_p.Ib - res_m.Ib) / (2.0 * delta);

        check_derivative("BJT NPN g_bc (dIb/dVc)", res.g_bc, dIb_dVc_fd);
        check_derivative("BJT NPN g_bb (dIb/dVb)", res.g_bb, dIb_dVb_fd);
        check_derivative("BJT NPN g_be (dIb/dVe)", res.g_be, dIb_dVe_fd);
    }
}

// ===========================================================================
int main() {
    std::cout << "=== AD Integration Test Suite ===\n";

    test_diode();
    test_mosfet();
    test_jfet();
    test_zener();
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
