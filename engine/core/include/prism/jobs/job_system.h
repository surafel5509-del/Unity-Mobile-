// =====================================================================
//  PRISM ENGINE — jobs/job_system.h
//  Work-stealing-free but mobile-tuned job graph: counter-based
//  dependencies, per-thread FIFO, pinned worker count from CPU topology.
//  Rationale (docs/07): on big.LITTLE SoCs, stealing across clusters
//  hurts more than it helps, so we use a shared ready queue with a
//  per-cluster affinity hint.
// =====================================================================
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>
#include <string>
#include "../core/types.h"

namespace prism {

using JobFn = std::function<void()>;

/// A handle to a group of jobs. `wait()` blocks until the counter hits zero.
class JobHandle {
public:
    JobHandle() = default;
    explicit JobHandle(std::shared_ptr<std::atomic<i32>> counter) : counter_(std::move(counter)) {}
    void add(i32 n) { if (counter_) counter_->fetch_add(n, std::memory_order_relaxed); }
    void complete() { if (counter_) counter_->fetch_sub(1, std::memory_order_release); }
    [[nodiscard]] bool done() const { return !counter_ || counter_->load(std::memory_order_acquire) <= 0; }
    [[nodiscard]] i32  remaining() const { return counter_ ? counter_->load(std::memory_order_acquire) : 0; }
    [[nodiscard]] std::shared_ptr<std::atomic<i32>> counter() const { return counter_; }
private:
    std::shared_ptr<std::atomic<i32>> counter_;
};

struct JobStats {
    u64 submitted = 0, completed = 0, waited = 0, stolen = 0;
    f64 busy_seconds = 0;
};

class JobSystem {
public:
    /// workers = 0 -> hardware_concurrency()-1 (clamped 1..8 for mobile thermals)
    explicit JobSystem(u32 workers = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    JobHandle schedule(JobFn fn);
    /// Fan-out: run `fn(index)` for index in [0,count) across the pool.
    JobHandle parallel_for(i32 count, const std::function<void(i32)>& fn, i32 grain = 1);
    /// Chain: `after` must complete before `fn` runs.
    JobHandle schedule_after(const JobHandle& after, JobFn fn);
    void wait(const JobHandle& h);
    void wait_all();
    void shutdown();

    [[nodiscard]] u32 worker_count() const { return worker_count_; }
    [[nodiscard]] u64 pending()      const { return pending_.load(std::memory_order_relaxed); }
    [[nodiscard]] JobStats stats()   const;
    /// Runs the queued work on the calling thread (used by tests + headless CI).
    void drain_on_caller();

private:
    void worker_loop();
    bool pop(JobFn& out);

    u32 worker_count_ = 0;
    std::vector<std::thread> workers_;
    std::deque<JobFn> queue_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<u64> pending_{0};
    mutable std::mutex stats_mu_;
    JobStats stats_;
};

/// Convenience: chunked parallel transform of [0,n) into out via fn.
template <typename T, typename Fn>
void parallel_map(JobSystem& js, const std::vector<T>& in, std::vector<T>& out, Fn fn) {
    out.resize(in.size());
    auto h = js.parallel_for(static_cast<i32>(in.size()), [&](i32 i) { out[i] = fn(in[i], i); });
    js.wait(h);
}

} // namespace prism
