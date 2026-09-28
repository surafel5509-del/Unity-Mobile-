// PRISM ENGINE — core/engine.h : the engine. Owns modules, clock, bus, services.
#pragma once
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include "../core/types.h"
#include "../core/module.h"
#include "../core/event.h"
#include "../core/clock.h"
#include "../core/log.h"
#include "../core/pool.h"

namespace prism {

/// Deterministic-ish RNG (xoshiro128**) so LAN rollback replays match exactly.
class Rng {
public:
    explicit Rng(u32 seed = 0x9E3779B9u) { this->seed(seed); }
    u32 next() {
        u32 s = state_[1] * 5u;
        u32 result = ((s << 7) | (s >> 25)) * 9u;
        u32 t = state_[1] << 9;
        state_[2] ^= state_[0]; state_[3] ^= state_[1];
        state_[1] ^= state_[2]; state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = (state_[3] << 11) | (state_[3] >> 21);
        return result;
    }
    f32 unit()  { return static_cast<f32>(next() >> 8) * (1.0f / 16777216.0f); }
    f32 range(f32 lo, f32 hi) { return lo + unit() * (hi - lo); }
    void seed(u32 s) { state_[0] = state_[1] = state_[2] = state_[3] = s ? s : 1u; }
private:
    u32 state_[4] = {0x12345678u, 0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u};

};

/// Quality governor: battery + thermal aware, drives adaptive resolution.
struct QualityProfile {
    std::string tier = "medium";     // low | medium | high | ultra
    f32 render_scale = 1.0f;
    bool shadows = true;
    bool post_processing = true;
    bool volumetric_fog = false;
    bool spectral_dispersion = true;
    i32  msaa_samples = 0;
    i32  max_dynamic_lights = 8;
};

class Engine {
public:
    explicit Engine(EngineConfig cfg = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Register a subsystem. Boot order is determined by Module::order().
    void add_module(std::unique_ptr<Module> m);

    /// Boots every module. Returns false (and logs) on the first failure.
    bool boot();
    void start();
    /// One frame: fixed physics steps + tick + render + bus events.
    void frame();
    void shutdown();
    /// Convenience for tests / headless runs: run N frames without rendering.
    void run_headless(int frames);

    [[nodiscard]] EventBus&       bus()      { return bus_; }
    [[nodiscard]] ServiceRegistry& services() { return services_; }
    [[nodiscard]] Clock&          clock()    { return clock_; }
    [[nodiscard]] Rng&            rng()      { return rng_; }
    [[nodiscard]] const EngineConfig& config() const { return config_; }
    [[nodiscard]] LinearAllocator& frame_arena() { return frame_arena_; }
    [[nodiscard]] bool running()   const { return state_ == State::Running; }

    /// GPU profile detection result (see platform/android/gpu_profile.cpp).
    void set_gpu_profile(std::string renderer, std::string vendor);
    [[nodiscard]] const std::string& gpu_renderer() const { return gpu_renderer_; }
    [[nodiscard]] const std::string& gpu_vendor()   const { return gpu_vendor_; }

    /// Battery / thermal governor input — called by the Android layer.
    void report_thermal(i32 thermal_status, f32 battery_level);
    [[nodiscard]] const QualityProfile& quality() const { return quality_; }
    void apply_quality(const std::string& tier);

    [[nodiscard]] Module* module(const std::string& name) const;
    template <typename T>
    T* module_as(const std::string& name) const { return static_cast<T*>(module(name)); }

    [[nodiscard]] u64 frame_count() const { return frame_count_; }
    [[nodiscard]] u64 fixed_steps() const { return fixed_steps_; }

    enum class State { Created, Booted, Running, Stopped };
    [[nodiscard]] State state() const { return state_; }

private:
    QualityProfile profile_for(const std::string& tier) const;

    EngineConfig config_;
    EventBus bus_;
    ServiceRegistry services_;
    Clock clock_;
    Rng rng_;
    LinearAllocator frame_arena_{4u << 20};
    std::vector<std::unique_ptr<Module>> modules_;
    State state_ = State::Created;
    u64 frame_count_ = 0, fixed_steps_ = 0;
    QualityProfile quality_;
    std::string gpu_renderer_ = "unknown", gpu_vendor_ = "unknown";
    i32 thermal_status_ = 0;
    f32 battery_ = 1.0f;
};

} // namespace prism
