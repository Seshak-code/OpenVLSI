# Orchestration Server Architecture

The `compute/orchestration/server` directory hosts the **Execution Listener** for remote compute workers.

## Logic: Request Fulfillment
It implements the gRPC or HTTP interfaces that receive subnets and simulation requests, dispatches them to the `local_worker_driver`, and returns the results.

## 🤖 SME Validation Checklist
- [ ] **Painless Scaling**: Does the server maintain state between independent requests? (Prefer stateless for easier scaling).
- [ ] **Concurrency**: Does the server handle multiple parallel job streams without deadlocking?
