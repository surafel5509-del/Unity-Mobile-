// PRISM ENGINE — core/module.h : module lifecycle + service registry.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <typeindex>
#include <functional>
#include "../core/types.h"
#include "../core/event.h"
#include "../core/log.h"

namespace prism {

struct EngineConfig {
    std::string app_name    = "PrismGame";
    std::string version     = "1.0.0";
    std::string data_dir    = ".";          // APK assets or editor project dir
    std::string writable_dir = ".";         // Android getFilesDir()
    i32  target_fps         = 60;
    bool vsync              = true;
    bool offline_mode       = true;         // always true: PRISM has no network features
    bool deterministic      = false;        // LAN rollback / replay
    std::string quality_tier = "auto";      // low|medium|high|ultra|auto
    u32  random_seed        = 0x9E3779B9u;
};

class Module;

/// Service registry: modules publish their public interface here, other modules
/// resolve it by name. No global singletons, no direct includes between modules.
class ServiceRegistry {
public:
    void publish(std::string name, std::shared_ptr<void> svc) {
        services_[std::move(name)] = std::move(svc);
    }
    template <typename T>
    std::shared_ptr<T> resolve(const std::string& name) const {
        auto it = services_.find(name);
        if (it == services_.end()) return nullptr;
        return std::static_pointer_cast<T>(it->second);
    }
    [[nodiscard]] bool has(const std::string& name) const { return services_.count(name) > 0; }
    [[nodiscard]] std::size_t size() const { return services_.size(); }
    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(services_.size());
        for (auto& kv : services_) out.push_back(kv.first);
        return out;
    }
private:
    std::unordered_map<std::string, std::shared_ptr<void>> services_;
};

/// Base class for every engine subsystem. Order of phases is fixed:
/// boot -> on_start -> (on_fixed_tick*, on_tick, on_render)* -> on_shutdown
class Module {
public:
    virtual ~Module() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual i32 order() const { return 100; }   // lower boots first

    virtual bool on_boot(const EngineConfig&, ServiceRegistry&, EventBus&) { return true; }
    virtual void on_start() {}
    virtual void on_fixed_tick(f32 /*fixed_dt*/) {}
    virtual void on_tick(f32 /*dt*/) {}
    virtual void on_render(f32 /*dt*/) {}
    virtual void on_shutdown() {}
    /// Called by the battery/thermal governor; modules may shed work.
    virtual void on_quality_changed(const std::string& /*tier*/, f32 /*render_scale*/) {}

    [[nodiscard]] bool booted() const { return booted_; }
    void mark_booted(bool v) { booted_ = v; }
private:
    bool booted_ = false;
};

} // namespace prism
