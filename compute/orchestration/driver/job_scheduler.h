#pragma once
// ============================================================================
// compute/orchestration/driver/job_scheduler.h — Thread-pool task scheduler
// ============================================================================
// Simple fixed-size thread pool. Enqueued tasks run concurrently on worker
// threads. On WASM (Emscripten), the pool runs with a single worker and
// executes tasks synchronously to avoid Emscripten threading limitations.
//
// Phase 4.3
// ============================================================================

#include <functional>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

namespace acutesim {
namespace compute {
namespace orchestration {

class JobScheduler {
public:
    // numWorkers == 0 → std::thread::hardware_concurrency()
    // On WASM, always clamps to 1 worker.
    explicit JobScheduler(int numWorkers = 0);
    ~JobScheduler();

    using Task = std::function<void()>;

    // Enqueue a task for async execution.
    // On WASM this executes the task synchronously inline.
    void enqueue(Task t);

    // Signal all workers to drain the queue and stop.
    // Blocks until all threads have exited.
    void shutdown();

    int workerCount() const;

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<Task>         queue_;
    std::mutex               mu_;
    std::condition_variable  cv_;
    std::atomic<bool>        stopped_{false};
};

} // namespace orchestration
} // namespace compute
} // namespace acutesim
