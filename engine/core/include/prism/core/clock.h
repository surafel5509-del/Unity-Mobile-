// PRISM ENGINE — core/clock.h : monotonic frame clock with fixed-step accumulator.
#pragma once
#include <chrono>
#include "../core/types.h"

namespace prism {

class Clock {
public:
    using Steady = std::chrono::steady_clock;

    explicit Clock(f32 fixed_dt = 1.0f / 60.0f) : fixed_dt_(fixed_dt) { reset(); }

    void reset() {
        start_ = Steady::now();
        last_  = start_;
        accumulator_ = 0.0f;
        frame_ = 0;
    }

    /// Call once per rendered frame. Returns seconds since the previous frame (clamped).
    f32 begin_frame() {
        auto now = Steady::now();
        f32 dt = std::chrono::duration<f32>(now - last_).count();
        last_ = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.25f) dt = 0.25f;               // avoid physics explosion after suspend
        delta_ = dt;
        time_ += dt;
        ++frame_;
        accumulator_ += dt;
        // FPS estimate (exponential moving average)
        if (dt > kEpsilon) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f;
        return dt;
    }

    /// Consume one fixed step; returns false when the accumulator is drained.
    bool take_fixed_step() {
        if (accumulator_ >= fixed_dt_) { accumulator_ -= fixed_dt_; return true; }
        return false;
    }

    [[nodiscard]] f32 delta()       const { return delta_; }
    [[nodiscard]] f32 time()        const { return time_; }
    [[nodiscard]] f32 fixed_dt()    const { return fixed_dt_; }
    [[nodiscard]] f32 fps()         const { return fps_; }
    [[nodiscard]] u64 frame()       const { return frame_; }
    [[nodiscard]] f32 alpha()       const { return accumulator_ / fixed_dt_; } // render interpolation
    void set_fixed_dt(f32 dt) { fixed_dt_ = dt; }

    [[nodiscard]] static f64 now_ms() {
        return std::chrono::duration<f64, std::milli>(Steady::now().time_since_epoch()).count();
    }

private:
    Steady::time_point start_{}, last_{};
    f32 fixed_dt_ = 1.0f / 60.0f;
    f32 delta_ = 0, time_ = 0, accumulator_ = 0, fps_ = 0;
    u64 frame_ = 0;
};

} // namespace prism
