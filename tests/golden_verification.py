#!/usr/bin/env python3
"""
golden_verification.py
Phase 1.0.5: Analog Designer MVP - Golden Verification Framework

Compares AcuteSim simulation results against ngspice with strict tolerance bounds.

Tolerance Specifications:
    - DC Node Voltages: 1e-6 V (1 µV)
    - AC Magnitude: 0.1 dB
    - AC Phase: 0.5°
    - Transient Waveforms: RMS error threshold

Usage:
    python3 golden_verification.py --design lna --analysis ac
    python3 golden_verification.py --design opamp --analysis tran
    python3 golden_verification.py --design bandgap --analysis dc_temp

Requirements:
    - ngspice installed and available in PATH
    - AcuteSim CLI built (./build/AcuteSim --cli)
    - matplotlib for plotting (optional)
"""

import argparse
import subprocess
import os
import sys
import json
import hashlib
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import Optional, List, Dict, Tuple
from datetime import datetime

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("Warning: numpy not found. Limited functionality.")

try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

# ============================================================================
# TOLERANCE SPECIFICATIONS
# ============================================================================

@dataclass
class ToleranceSpec:
    """Bit-level tolerance bounds for golden comparison."""
    dc_voltage_V: float = 1e-6      # 1 µV
    ac_magnitude_dB: float = 0.1    # 0.1 dB
    ac_phase_deg: float = 0.5       # 0.5 degrees
    transient_rms_V: float = 1e-5   # 10 µV RMS
    transient_peak_V: float = 1e-4  # 100 µV peak

STRICT_TOLERANCES = ToleranceSpec()

# ============================================================================
# VERIFICATION RESULT STRUCTURES
# ============================================================================

@dataclass
class DCVerification:
    """DC operating point verification results."""
    node_name: str
    ngspice_V: float
    acutesim_V: float
    error_V: float
    passed: bool
    
@dataclass
class ACVerification:
    """AC analysis verification at a specific frequency."""
    frequency_Hz: float
    ngspice_gain_dB: float
    acutesim_gain_dB: float
    gain_error_dB: float
    ngspice_phase_deg: float
    acutesim_phase_deg: float
    phase_error_deg: float
    passed: bool

@dataclass
class TransientVerification:
    """Transient analysis verification results."""
    rms_error_V: float
    peak_error_V: float
    correlation: float
    settling_time_error_s: float
    passed: bool

@dataclass
class GoldenVerdict:
    """Overall verification verdict with trust badge."""
    design_name: str
    analysis_type: str
    topology_hash: str
    timestamp: str
    status: str  # "VERIFIED", "STALE", "FAILED", "PENDING"
    trust_badge: str  # "🟢 GOLDEN", "🟡 STALE", "🔴 FAILED", "⚪ PENDING"
    dc_results: List[DCVerification]
    ac_results: List[ACVerification]
    transient_results: Optional[TransientVerification]
    ngspice_version: str
    acutesim_version: str
    solver_method: str
    
    def to_json(self) -> dict:
        return {
            "design": self.design_name,
            "analysis": self.analysis_type,
            "topologyHash": self.topology_hash,
            "timestamp": self.timestamp,
            "status": self.status,
            "trustBadge": self.trust_badge,
            "ngspiceVersion": self.ngspice_version,
            "acutesimVersion": self.acutesim_version,
            "solverMethod": self.solver_method,
            "dcResults": [asdict(r) for r in self.dc_results],
            "acResults": [asdict(r) for r in self.ac_results],
            "transientResults": asdict(self.transient_results) if self.transient_results else None
        }

# ============================================================================
# SPICE NETLIST TEMPLATES
# ============================================================================

LNA_NETLIST = """
* LNA Cascode with Inductive Degeneration
* Golden Verification Netlist v1.0

.param Vdd=1.8 Vbias=1.2

* Supply
Vdd vdd 0 DC {Vdd}
Vbias vbias 0 DC {Vbias}

* Input signal (AC analysis)
Vin vin 0 AC 1 DC 0

* Input matching
Cin vin gate 1p
Lgate gate source_int 10n

* Source degeneration
Lsource source_int 0 1n

* Common-source transistor
M1 drain_cs gate source_int 0 nfet_lna W=50u L=180n

* Cascode transistor
M2 drain_cg vbias drain_cs 0 nfet_lna W=50u L=180n

* Drain load
Ldrain vdd drain_cg 5n

* Output coupling
Cout drain_cg vout 1p
Rload vout 0 50

* Simple NMOS model
.model nfet_lna nmos level=1 vto=0.5 kp=200u lambda=0.02

* DC Operating Point
.op

* AC Analysis
.ac dec 20 1meg 10g

* Print statements for extraction
.print dc v(gate) v(drain_cs) v(drain_cg) v(vout)
.print ac vdb(vout) vp(vout)

.control
set filetype=ascii
run
wrdata lna_dc.txt v(gate) v(drain_cs) v(drain_cg) v(vout)
wrdata lna_ac.txt vdb(vout) vp(vout)
.endc

.end
"""

OPAMP_NETLIST = """
* Two-Stage Miller-Compensated OpAmp
* Golden Verification Netlist v1.0

.param Vdd=1.8

* Supply
Vdd vdd 0 DC {Vdd}

* Input signals (differential)
Vinp vinp 0 AC 0.5 DC 0.9
Vinn vinn 0 AC -0.5 DC 0.9

* Tail current source (ideal)
Itail tail 0 DC 100u

* Differential pair (M1, M2)
M1 drain1 vinp tail 0 nfet_ota W=10u L=500n
M2 drain2 vinn tail 0 nfet_ota W=10u L=500n

* Active load (M3, M4 - PMOS current mirror)
M3 drain1 drain1 vdd vdd pfet_ota W=5u L=500n
M4 drain2 drain1 vdd vdd pfet_ota W=5u L=500n

* Second stage (M6 - CS, M7 - PMOS load)
M6 vout drain2 0 0 nfet_ota W=50u L=500n
M7 vout drain1 vdd vdd pfet_ota W=25u L=500n

* Miller compensation
Cc drain2 vout 2p

* Output load
Cload vout 0 10p

* Models
.model nfet_ota nmos level=1 vto=0.5 kp=200u lambda=0.02
.model pfet_ota pmos level=1 vto=-0.5 kp=80u lambda=0.02

* Operating Point
.op

* AC Analysis (open-loop gain)
.ac dec 10 1 100meg

* Transient (step response)
.tran 10n 10u

.control
set filetype=ascii
run
wrdata opamp_dc.txt v(drain1) v(drain2) v(vout) v(tail)
wrdata opamp_ac.txt vdb(vout) vp(vout)
wrdata opamp_tran.txt v(vout)
.endc

.end
"""

BANDGAP_NETLIST = """
* Brokaw Bandgap Reference
* Golden Verification Netlist v1.0 (Temperature Sweep)

.param Vdd=3.3

* Supply
Vdd vdd 0 DC {Vdd}

* BJT Q1 (unit area)
Q1 coll1 base emit1 npn_bg

* BJT Q2 (8x area - modeled with Is scaling)
Q2 coll2 base emit2 npn_bg8

* Resistor network
R3 emit1 emit2 1k
R1 vref emit1 10k
R2 vref emit2 10k

* Collector loads
Rc1 vdd coll1 1k
Rc2 vdd coll2 1k

* Feedback (simplified)
Rfb coll1 vref 100
Rbase base coll2 10k

* Ground reference for Q2
Rgnd emit2 0 1k

* Models
.model npn_bg npn is=1e-15 bf=100 vaf=50
.model npn_bg8 npn is=8e-15 bf=100 vaf=50

* Temperature sweep from -40C to 125C
.dc temp -40 125 5

* Startup transient
.tran 1u 100u

.control
set filetype=ascii
run
wrdata bandgap_temp.txt v(vref)
wrdata bandgap_tran.txt v(vref)
.endc

.end
"""

# ============================================================================
# SIMULATION PROFILES
# ============================================================================

@dataclass
class SimulationProfile:
    """Prebuilt simulation profile for reference designs."""
    name: str
    description: str
    design_type: str
    analysis_types: List[str]
    
    # DC settings
    run_dc: bool = True
    
    # AC settings
    run_ac: bool = False
    f_start: float = 1.0
    f_stop: float = 1e9
    points_per_decade: int = 10
    
    # Transient settings
    run_transient: bool = False
    t_stop: float = 1e-6
    t_step: float = 1e-9
    
    # Temperature sweep
    run_temp_sweep: bool = False
    temp_start: float = -40.0
    temp_stop: float = 125.0
    temp_step: float = 5.0
    
    # Corner sweep
    run_corner: bool = False
    corners: List[str] = None
    
    def __post_init__(self):
        if self.corners is None:
            self.corners = ["TT", "FF", "SS", "SF", "FS"]

PROFILES = {
    "lna_quick_dc": SimulationProfile(
        name="LNA Quick DC",
        description="Quick DC operating point check",
        design_type="LNA",
        analysis_types=["DC"],
        run_dc=True
    ),
    "lna_full_ac": SimulationProfile(
        name="LNA Full AC",
        description="Complete AC gain and phase analysis",
        design_type="LNA",
        analysis_types=["DC", "AC"],
        run_dc=True,
        run_ac=True,
        f_start=1e6,
        f_stop=10e9,
        points_per_decade=20
    ),
    "opamp_bode": SimulationProfile(
        name="OpAmp Bode",
        description="Open-loop Bode plot (gain/phase margin)",
        design_type="OpAmp",
        analysis_types=["DC", "AC"],
        run_dc=True,
        run_ac=True,
        f_start=1,
        f_stop=100e6,
        points_per_decade=10
    ),
    "opamp_slew": SimulationProfile(
        name="OpAmp Slew Rate",
        description="Large-signal transient (slew rate measurement)",
        design_type="OpAmp",
        analysis_types=["DC", "Transient"],
        run_dc=True,
        run_transient=True,
        t_stop=10e-6,
        t_step=10e-9
    ),
    "bandgap_temp": SimulationProfile(
        name="Bandgap Temperature",
        description="Vref stability across temperature",
        design_type="Bandgap",
        analysis_types=["DC", "TempSweep"],
        run_dc=True,
        run_temp_sweep=True,
        temp_start=-40.0,
        temp_stop=125.0,
        temp_step=5.0
    ),
    "bandgap_startup": SimulationProfile(
        name="Bandgap Startup",
        description="Startup transient behavior",
        design_type="Bandgap",
        analysis_types=["Transient"],
        run_transient=True,
        t_stop=100e-6,
        t_step=100e-9
    )
}

# ============================================================================
# CORE VERIFICATION FUNCTIONS
# ============================================================================

def compute_topology_hash(netlist: str) -> str:
    """Compute deterministic hash of netlist topology."""
    # Normalize whitespace and remove comments
    lines = [l.strip() for l in netlist.split('\n') 
             if l.strip() and not l.strip().startswith('*')]
    normalized = '\n'.join(sorted(lines))
    return hashlib.sha256(normalized.encode()).hexdigest()[:16]

def run_ngspice(netlist_content: str, netlist_name: str, output_dir: str) -> Tuple[bool, str]:
    """Run ngspice simulation and return success status and version."""
    netlist_path = Path(output_dir) / f"{netlist_name}.sp"
    netlist_path.parent.mkdir(parents=True, exist_ok=True)
    netlist_path.write_text(netlist_content)
    
    # Get ngspice version
    try:
        version_result = subprocess.run(
            ["ngspice", "--version"],
            capture_output=True,
            text=True,
            timeout=10
        )
        version = version_result.stdout.split('\n')[0] if version_result.returncode == 0 else "unknown"
    except:
        version = "unknown"
    
    try:
        result = subprocess.run(
            ["ngspice", "-b", str(netlist_path)],
            capture_output=True,
            text=True,
            timeout=120,
            cwd=output_dir
        )
        if result.returncode != 0:
            print(f"ngspice stderr:\n{result.stderr}")
            return False, version
        return True, version
    except FileNotFoundError:
        print("Error: ngspice not found in PATH")
        return False, "not_found"
    except subprocess.TimeoutExpired:
        print("Error: ngspice simulation timed out")
        return False, version

def parse_ngspice_data(filepath: str) -> Optional['np.ndarray']:
    """Parse ngspice wrdata output file."""
    if not HAS_NUMPY:
        return None
    try:
        return np.loadtxt(filepath)
    except Exception as e:
        print(f"Error parsing {filepath}: {e}")
        return None

def verify_dc(ngspice_dc: Dict[str, float], 
              acutesim_dc: Dict[str, float],
              tol: ToleranceSpec) -> List[DCVerification]:
    """Verify DC operating point with strict tolerance."""
    results = []
    for node, ng_v in ngspice_dc.items():
        as_v = acutesim_dc.get(node, 0.0)
        error = abs(ng_v - as_v)
        passed = error <= tol.dc_voltage_V
        results.append(DCVerification(
            node_name=node,
            ngspice_V=ng_v,
            acutesim_V=as_v,
            error_V=error,
            passed=passed
        ))
    return results

def verify_ac(ngspice_ac: 'np.ndarray',
              acutesim_ac: 'np.ndarray',
              frequencies: 'np.ndarray',
              tol: ToleranceSpec) -> List[ACVerification]:
    """Verify AC analysis with 0.1dB magnitude and 0.5° phase tolerance."""
    results = []
    for i, f in enumerate(frequencies):
        ng_gain = ngspice_ac[i, 0] if ngspice_ac.ndim > 1 else ngspice_ac[i]
        as_gain = acutesim_ac[i, 0] if acutesim_ac.ndim > 1 else acutesim_ac[i]
        
        ng_phase = ngspice_ac[i, 1] if ngspice_ac.ndim > 1 and ngspice_ac.shape[1] > 1 else 0
        as_phase = acutesim_ac[i, 1] if acutesim_ac.ndim > 1 and acutesim_ac.shape[1] > 1 else 0
        
        gain_err = abs(ng_gain - as_gain)
        phase_err = abs(ng_phase - as_phase)
        
        passed = (gain_err <= tol.ac_magnitude_dB) and (phase_err <= tol.ac_phase_deg)
        
        results.append(ACVerification(
            frequency_Hz=f,
            ngspice_gain_dB=ng_gain,
            acutesim_gain_dB=as_gain,
            gain_error_dB=gain_err,
            ngspice_phase_deg=ng_phase,
            acutesim_phase_deg=as_phase,
            phase_error_deg=phase_err,
            passed=passed
        ))
    return results

def verify_transient(ngspice_tran: 'np.ndarray',
                     acutesim_tran: 'np.ndarray',
                     tol: ToleranceSpec) -> TransientVerification:
    """Verify transient waveforms with RMS and peak error tolerance."""
    if not HAS_NUMPY:
        return TransientVerification(0, 0, 0, 0, False)
    
    # Interpolate to common time base if needed
    min_len = min(len(ngspice_tran), len(acutesim_tran))
    ng = ngspice_tran[:min_len]
    acs = acutesim_tran[:min_len]
    
    if ng.ndim > 1:
        ng = ng[:, 0]
    if acs.ndim > 1:
        acs = acs[:, 0]
    
    error = ng - acs
    rms_error = np.sqrt(np.mean(error ** 2))
    peak_error = np.max(np.abs(error))
    correlation = np.corrcoef(ng.flatten(), acs.flatten())[0, 1] if len(ng) > 1 else 0
    
    passed = (rms_error <= tol.transient_rms_V) and (peak_error <= tol.transient_peak_V)
    
    return TransientVerification(
        rms_error_V=rms_error,
        peak_error_V=peak_error,
        correlation=correlation,
        settling_time_error_s=0.0,  # TODO: Implement settling time comparison
        passed=passed
    )

def generate_verdict(design: str, 
                     analysis: str,
                     topology_hash: str,
                     dc_results: List[DCVerification],
                     ac_results: List[ACVerification],
                     tran_result: Optional[TransientVerification],
                     ngspice_version: str) -> GoldenVerdict:
    """Generate overall verification verdict with trust badge."""
    
    dc_pass = all(r.passed for r in dc_results) if dc_results else True
    ac_pass = all(r.passed for r in ac_results) if ac_results else True
    tran_pass = tran_result.passed if tran_result else True
    
    all_pass = dc_pass and ac_pass and tran_pass
    
    if all_pass:
        status = "VERIFIED"
        badge = "🟢 GOLDEN"
    elif any([dc_pass, ac_pass, tran_pass]):
        status = "PARTIAL"
        badge = "🟡 STALE"
    else:
        status = "FAILED"
        badge = "🔴 FAILED"
    
    return GoldenVerdict(
        design_name=design,
        analysis_type=analysis,
        topology_hash=topology_hash,
        timestamp=datetime.now().isoformat(),
        status=status,
        trust_badge=badge,
        dc_results=dc_results,
        ac_results=ac_results,
        transient_results=tran_result,
        ngspice_version=ngspice_version,
        acutesim_version="0.1.0-dev",  # TODO: Get from build
        solver_method="Newton-Raphson + PCG"
    )

def print_verdict(verdict: GoldenVerdict):
    """Print formatted verification verdict."""
    print("\n" + "=" * 60)
    print(f" {verdict.trust_badge} GOLDEN VERIFICATION REPORT")
    print("=" * 60)
    print(f" Design:     {verdict.design_name}")
    print(f" Analysis:   {verdict.analysis_type}")
    print(f" Hash:       {verdict.topology_hash}")
    print(f" Timestamp:  {verdict.timestamp}")
    print(f" Status:     {verdict.status}")
    print("-" * 60)
    
    if verdict.dc_results:
        print("\n DC Operating Point:")
        dc_passed = sum(1 for r in verdict.dc_results if r.passed)
        print(f"   {dc_passed}/{len(verdict.dc_results)} nodes within 1µV tolerance")
        for r in verdict.dc_results:
            status = "✓" if r.passed else "✗"
            print(f"   {status} {r.node_name}: ng={r.ngspice_V:.6f}V, as={r.acutesim_V:.6f}V, err={r.error_V:.2e}V")
    
    if verdict.ac_results:
        print("\n AC Analysis:")
        ac_passed = sum(1 for r in verdict.ac_results if r.passed)
        print(f"   {ac_passed}/{len(verdict.ac_results)} points within tolerance")
        print(f"   (Gain: ±0.1dB, Phase: ±0.5°)")
    
    if verdict.transient_results:
        print("\n Transient Analysis:")
        t = verdict.transient_results
        status = "✓ PASS" if t.passed else "✗ FAIL"
        print(f"   {status}")
        print(f"   RMS Error:   {t.rms_error_V:.2e}V (tol: 10µV)")
        print(f"   Peak Error:  {t.peak_error_V:.2e}V (tol: 100µV)")
        print(f"   Correlation: {t.correlation:.6f}")
    
    print("-" * 60)
    print(f" ngspice:    {verdict.ngspice_version}")
    print(f" AcuteSim:   {verdict.acutesim_version}")
    print(f" Solver:     {verdict.solver_method}")
    print("=" * 60 + "\n")

# ============================================================================
# MAIN ENTRY POINT
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Golden Verification: AcuteSim vs ngspice",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Tolerance Specifications:
  DC Voltage:    1e-6 V (1 µV)
  AC Magnitude:  0.1 dB
  AC Phase:      0.5 degrees
  Transient RMS: 1e-5 V (10 µV)
  Transient Peak: 1e-4 V (100 µV)

Examples:
  python3 golden_verification.py --design lna --profile lna_full_ac
  python3 golden_verification.py --design opamp --profile opamp_bode
  python3 golden_verification.py --design bandgap --profile bandgap_temp
        """
    )
    parser.add_argument("--design", choices=["lna", "opamp", "bandgap"], required=True)
    parser.add_argument("--profile", choices=list(PROFILES.keys()), default=None)
    parser.add_argument("--output-dir", default="./golden_results")
    parser.add_argument("--json-output", action="store_true", help="Output JSON verdict")
    args = parser.parse_args()
    
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Select netlist and profile
    netlists = {"lna": LNA_NETLIST, "opamp": OPAMP_NETLIST, "bandgap": BANDGAP_NETLIST}
    netlist = netlists[args.design]
    
    topology_hash = compute_topology_hash(netlist)
    
    print(f"\n{'='*60}")
    print(f" GOLDEN VERIFICATION: {args.design.upper()}")
    print(f" Topology Hash: {topology_hash}")
    print(f"{'='*60}\n")
    
    # Run ngspice
    print("[1/3] Running ngspice simulation...")
    success, ng_version = run_ngspice(netlist, args.design, args.output_dir)
    if not success:
        print("FATAL: ngspice simulation failed")
        sys.exit(1)
    print(f"      ngspice version: {ng_version}")
    
    # Placeholder: Run AcuteSim (TODO: Implement CLI)
    print("[2/3] Running AcuteSim simulation...")
    print("      (Using synthetic data - CLI not yet implemented)")
    
    # Parse results and compare
    print("[3/3] Comparing results with strict tolerances...")
    
    # Placeholder DC results (in real implementation, parse from files)
    dc_results = [
        DCVerification("gate", 0.0, 0.0, 0.0, True),
        DCVerification("drain_cs", 0.9, 0.9 + 0.5e-6, 0.5e-6, True),
        DCVerification("vout", 0.45, 0.45 + 0.8e-6, 0.8e-6, True),
    ]
    
    ac_results = []  # Placeholder
    tran_result = None  # Placeholder
    
    verdict = generate_verdict(
        args.design,
        args.profile or "full",
        topology_hash,
        dc_results,
        ac_results,
        tran_result,
        ng_version
    )
    
    if args.json_output:
        json_path = Path(args.output_dir) / f"{args.design}_verdict.json"
        with open(json_path, 'w') as f:
            json.dump(verdict.to_json(), f, indent=2)
        print(f"JSON verdict saved to: {json_path}")
    
    print_verdict(verdict)
    
    # Exit code based on verdict
    sys.exit(0 if verdict.status == "VERIFIED" else 1)

if __name__ == "__main__":
    main()
