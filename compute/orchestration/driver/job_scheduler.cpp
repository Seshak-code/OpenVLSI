#include "compute/orchestration/driver/job_scheduler.h"
#include <algorithm>

namespace acutesim {
namespace compute {
namespace orchestration {

JobScheduler::JobScheduler(int numWorkers) {
#ifdef __EMSCRIPTEN__
    // Emscripten does not provide a reliable multi-threading environment
    // for synchronous task dispatch. Run single-threaded.
    (void)numWorkers;
    // No worker threads — enqueue() runs synchronously.
#else
    if (numWorkers <= 0)
        numWorkers = static_cast<int>(std::thread::hardware_concurrency());
    numWorkers = std::max(1, numWorkers);

    workers_.reserve(static_cast<size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i)
        workers_.emplace_back([this] { workerLoop(); });
#endif
}

JobScheduler::~JobScheduler() {
    shutdown();
}

void JobScheduler::enqueue(Task t) {
#ifdef __EMSCRIPTEN__
    // Synchronous inline execution on WASM — no threading.
    t();
#else
    {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push_back(std::move(t));
    }
    cv_.notify_one();
#endif
}

void JobScheduler::shutdown() {
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lk(mu_);
        stopped_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_)
        if (w.joinable()) w.join();
    workers_.clear();
#endif
}

int JobScheduler::workerCount() const {
#ifdef __EMSCRIPTEN__
    return 1; // logical single worker
#else
    return static_cast<int>(workers_.size());
#endif
}

void JobScheduler::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return stopped_.load() || !queue_.empty();
            });
            if (stopped_.load() && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }
}

} // namespace orchestration
} // namespace compute
} // namespace acutesim
