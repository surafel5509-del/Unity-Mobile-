#include "prism/core/engine.h"
#include <algorithm>

namespace prism {

Engine::Engine(EngineConfig cfg) : config_(std::move(cfg)), rng_(config_.random_seed) {
    clock_.set_fixed_dt(1.0f / static_cast<f32>(config_.target_fps > 0 ? config_.target_fps : 60));
    quality_ = profile_for(config_.quality_tier == "auto" ? "medium" : config_.quality_tier);
}

Engine::~Engine() {
    if (state_ == State::Running) shutdown();
}

void Engine::add_module(std::unique_ptr<Module> m) {
    if (!m) return;
    modules_.push_back(std::move(m));
    std::stable_sort(modules_.begin(), modules_.end(),
                     [](const std::unique_ptr<Module>& a, const std::unique_ptr<Module>& b) {
                         return a->order() < b->order();
                     });
}

bool Engine::boot() {
    if (state_ != State::Created) return state_ != State::Stopped;
    PRISM_INFO("engine", std::string("PRISM ENGINE ") + kEngineVersion.str() + " booting (APK-only, offline-first)");
    for (auto& m : modules_) {
        bool ok = m->on_boot(config_, services_, bus_);
        if (!ok) {
            PRISM_ERROR("engine", std::string("module failed to boot: ") + m->name());
            return false;
        }
        m->mark_booted(true);
        PRISM_INFO("engine", std::string("module booted: ") + m->name());
    }
    state_ = State::Booted;
    return true;
}

void Engine::start() {
    if (state_ != State::Booted) return;
    for (auto& m : modules_) m->on_start();
    state_ = State::Running;
    clock_.reset();
    bus_.publish(AppLifecycle{"resume"});
    PRISM_INFO("engine", std::string("engine running: ") + config_.app_name + std::string(" v") + config_.version);
}

void Engine::frame() {
    if (state_ != State::Running) return;
    f32 dt = clock_.begin_frame();
    frame_arena_.rewind();

    // --- fixed-step simulation (physics, netcode, AI) --------------------
    u64 guard = 0;
    while (clock_.take_fixed_step() && guard++ < 8) {
        ++fixed_steps_;
        for (auto& m : modules_) m->on_fixed_tick(clock_.fixed_dt());
        bus_.publish(FixedTickEvent{clock_.fixed_dt(), fixed_steps_});
    }

    // --- variable tick (gameplay, animation, UI) -------------------------
    for (auto& m : modules_) m->on_tick(dt);
    bus_.publish(TickEvent{dt, clock_.time(), clock_.frame()});

    // --- render ----------------------------------------------------------
    for (auto& m : modules_) m->on_render(dt);

    ++frame_count_;
}

void Engine::run_headless(int frames) {
    if (state_ == State::Booted) start();
    for (int i = 0; i < frames && state_ == State::Running; ++i) frame();
}

void Engine::shutdown() {
    if (state_ == State::Stopped) return;
    bus_.publish(AppLifecycle{"pause"});
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) (*it)->on_shutdown();
    state_ = State::Stopped;
    PRISM_INFO("engine", "engine shutdown complete");
}

Module* Engine::module(const std::string& name) const {
    for (auto& m : modules_) if (m->name() == name) return m.get();
    return nullptr;
}

QualityProfile Engine::profile_for(const std::string& tier) const {
    QualityProfile p;
    p.tier = tier;
    if (tier == "low") {
        p.render_scale = 0.6f; p.shadows = false; p.post_processing = false;
        p.volumetric_fog = false; p.spectral_dispersion = false;
        p.msaa_samples = 0; p.max_dynamic_lights = 2;
    } else if (tier == "medium") {
        p.render_scale = 0.8f; p.shadows = true; p.post_processing = true;
        p.volumetric_fog = false; p.spectral_dispersion = true;
        p.msaa_samples = 0; p.max_dynamic_lights = 4;
    } else if (tier == "high") {
        p.render_scale = 1.0f; p.shadows = true; p.post_processing = true;
        p.volumetric_fog = true; p.spectral_dispersion = true;
        p.msaa_samples = 4; p.max_dynamic_lights = 8;
    } else { // ultra
        p.render_scale = 1.0f; p.shadows = true; p.post_processing = true;
        p.volumetric_fog = true; p.spectral_dispersion = true;
        p.msaa_samples = 4; p.max_dynamic_lights = 16;
    }
    return p;
}

void Engine::set_gpu_profile(std::string renderer, std::string vendor) {
    gpu_renderer_ = std::move(renderer);
    gpu_vendor_   = std::move(vendor);
    // Auto tier from GPU family (see docs/06-rendering-pipeline.md, GPU profiles).
    std::string t = "medium";
    std::string r = gpu_renderer_;
    auto has = [&](const char* s) { return r.find(s) != std::string::npos; };
    if (has("Adreno 7") || has("Mali-G7") || has("Xclipse") || has("Immortalis")) t = "ultra";
    else if (has("Adreno 6") || has("Mali-G6") || has("PowerVR B")) t = "high";
    else if (has("Adreno 5") || has("Mali-G5") || has("PowerVR")) t = "medium";
    else if (has("Adreno 3") || has("Mali-4") || has("Mali-T")) t = "low";
    if (config_.quality_tier == "auto") apply_quality(t);
    PRISM_INFO("engine", std::string("GPU profile: ") + gpu_vendor_ + std::string(" / ") + gpu_renderer_ + std::string(" -> tier ") + quality_.tier);
}

void Engine::apply_quality(const std::string& tier) {
    quality_ = profile_for(tier);
    for (auto& m : modules_) m->on_quality_changed(quality_.tier, quality_.render_scale);
    bus_.publish(QualityChanged{quality_.tier, quality_.render_scale});
}

void Engine::report_thermal(i32 thermal_status, f32 battery_level) {
    thermal_status_ = thermal_status;
    battery_ = battery_level;
    bus_.publish(ThermalEvent{thermal_status_, battery_});
    // Battery-aware + thermal governor: step the tier down, never up automatically.
    static const char* ladder[] = {"low", "medium", "high", "ultra"};
    int idx = 1;
    for (int i = 0; i < 4; ++i) if (quality_.tier == ladder[i]) idx = i;
    bool downgrade = thermal_status_ >= 2 || battery_ < 0.15f;
    if (downgrade && idx > 0) apply_quality(ladder[idx - 1]);
}

} // namespace prism
