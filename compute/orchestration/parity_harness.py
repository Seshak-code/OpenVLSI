import sys
import grpc
import os
import time
import argparse

# Add generated proto path
sys.path.append(os.path.join(os.path.dirname(__file__), "proto"))

try:
    import worker_service_pb2
    import worker_service_pb2_grpc
except ImportError:
    print("FATAL: Protobuf modules not found. Ensure 'make acutesim-worker' has run.")
    sys.exit(1)

def run(args):
    target = 'localhost:50051'
    print(f"[HARNESS] Connecting to Worker Agent at {target}...")
    
    channel = grpc.insecure_channel(target)
    stub = worker_service_pb2_grpc.WorkerServiceStub(channel)
    
    try:
        # Check Readiness
        status = stub.GetStatus(worker_service_pb2.StatusRequest())
        if not status.ready:
            print("[HARNESS] Worker is not ready.")
            return False
            
        print(f"[HARNESS] Connected to Worker: {status.worker_id} (Ver: {status.version})")
        print(f"[HARNESS] Dispatching Regression Suite: '{args.suite}' | Mode: {args.solver_mode} | Precision: {args.precision} ...")
        
        req = worker_service_pb2.RegressionRequest(
            job_id=f"reg-{int(time.time())}", 
            suite_name=args.suite,
            solver_mode=args.solver_mode,
            precision=args.precision
        )
        
        passed = False
        print("-" * 50)
        for log in stub.RunRegression(req):
            print(f"[WORKER] {log.log_line}")
            if log.is_finished:
                passed = log.passed
        print("-" * 50)
        
        if passed:
            print("[HARNESS] Regression Result: PASS (Numerical Authority Confirmed)")
            if args.update_golden:
               print("[HARNESS] WARN: Golden DB update requested but not yet implemented in worker logic.")
            return True
        else:
            print("[HARNESS] Regression Result: FAIL")
            return False

    except grpc.RpcError as e:
        print(f"[HARNESS] RPC Failed: {e.code()} - {e.details()}")
        return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="AcuteSim Regression Parity Harness")
    parser.add_argument("--suite", default="all", help="Regression suite name (all, fpu_determinism)")
    parser.add_argument("--solver_mode", default="cpu", choices=["cpu", "cpu_simd", "gpu"], help="Solver backend mode")
    parser.add_argument("--precision", default="fp64", choices=["fp64", "fp64_emulated"], help="Floating point precision")
    parser.add_argument("--update-golden", action="store_true", help="Allow updating the Golden DB (Requires dev privileges)")
    
    args = parser.parse_args()
    
    success = run(args)
    sys.exit(0 if success else 1)
