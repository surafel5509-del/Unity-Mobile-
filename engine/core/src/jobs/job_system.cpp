#include "prism/jobs/job_system.h"
#include <algorithm>
#include <chrono>
#include "prism/core/log.h"

namespace prism {

JobSystem::JobSystem(u32 workers) {
    if (workers == 0) {
        u32 hw = std::thread::hardware_concurrency();
        workers = hw > 1 ? hw - 1 : 1;
        workers = std::min<u32>(workers, 8u);      // cap: mobile thermal budget
    }
    worker_count_ = workers;
    for (u32 i = 0; i < worker_count_; ++i)
        workers_.emplace_back([this] { worker_loop(); });
    PRISM_INFO("jobs", std::string("job system online: ") + std::to_string(worker_count_) + " workers");
}

JobSystem::~JobSystem() { shutdown(); }

JobHandle JobSystem::schedule(JobFn fn) {
    auto counter = std::make_shared<std::atomic<i32>>(1);
    JobHandle h(counter);
    {
        std::lock_guard<std::mutex> g(mu_);
        queue_.push_back([this, fn = std::move(fn), counter]() mutable {
            auto t0 = std::chrono::steady_clock::now();
            fn();
            auto t1 = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> sg(stats_mu_);
                stats_.completed++;
                stats_.busy_seconds += std::chrono::duration<f64>(t1 - t0).count();
            }
            counter->fetch_sub(1, std::memory_order_release);
        });
        pending_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> sg(stats_mu_);
            stats_.submitted++;
        }
    }
    cv_.notify_one();
    return h;
}

JobHandle JobSystem::parallel_for(i32 count, const std::function<void(i32)>& fn, i32 grain) {
    if (count <= 0) return JobHandle{};
    if (grain < 1) grain = 1;
    i32 chunks = (count + grain - 1) / grain;
    auto counter = std::make_shared<std::atomic<i32>>(0);
    JobHandle h(counter);
    counter->store(chunks, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> g(mu_);
        for (i32 c = 0; c < chunks; ++c) {
            i32 begin = c * grain;
            i32 end   = std::min<i32>(begin + grain, count);
            queue_.push_back([this, fn, begin, end, counter]() {
                for (i32 i = begin; i < end; ++i) fn(i);
                {
                    std::lock_guard<std::mutex> sg(stats_mu_);
                    stats_.completed++;
                }
                counter->fetch_sub(1, std::memory_order_release);
            });
            pending_.fetch_add(1, std::memory_order_relaxed);
        }
        {
            std::lock_guard<std::mutex> sg(stats_mu_);
            stats_.submitted += static_cast<u64>(chunks);
        }
    }
    cv_.notify_all();
    return h;
}

JobHandle JobSystem::schedule_after(const JobHandle& after, JobFn fn) {
    auto self = this;
    return schedule([self, after, fn = std::move(fn)]() {
        self->wait(after);
        fn();
    });
}

void JobSystem::wait(const JobHandle& h) {
    if (h.done()) return;
    {
        std::lock_guard<std::mutex> sg(stats_mu_);
        stats_.waited++;
    }
    // Help the pool while spinning so the caller thread is never idle.
    JobFn job;
    while (!h.done()) {
        if (pop(job)) {
            job();
            pending_.fetch_sub(1, std::memory_order_relaxed);
        } else {
            std::this_thread::yield();
        }
    }
}

void JobSystem::wait_all() {
    JobFn job;
    while (pending_.load(std::memory_order_relaxed) > 0) {
        if (pop(job)) {
            job();
            pending_.fetch_sub(1, std::memory_order_relaxed);
        } else {
            std::this_thread::yield();
        }
    }
}

void JobSystem::drain_on_caller() { wait_all(); }

bool JobSystem::pop(JobFn& out) {
    std::lock_guard<std::mutex> g(mu_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void JobSystem::worker_loop() {
    for (;;) {
        JobFn job;
        {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [this] { return stop_.load() || !queue_.empty(); });
            if (stop_.load() && queue_.empty()) return;
            if (queue_.empty()) continue;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job();
        pending_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void JobSystem::shutdown() {
    if (stop_.exchange(true)) { cv_.notify_all(); for (auto& t : workers_) if (t.joinable()) t.join(); return; }
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    PRISM_INFO("jobs", "job system stopped");
}

JobStats JobSystem::stats() const {
    std::lock_guard<std::mutex> g(stats_mu_);
    return stats_;
}

} // namespace prism
