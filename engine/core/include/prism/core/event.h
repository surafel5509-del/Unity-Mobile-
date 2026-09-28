// PRISM ENGINE — core/event.h : typed event bus. Every subsystem publishes here.
#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <any>
#include "../core/types.h"

namespace prism {

using EventName = std::string;
using ListenerId = u64;

/// Type-erased, thread-safe publish/subscribe bus.
/// The bus is the ONLY cross-module channel: render, physics, audio, AI, net,
/// input and UI never call each other directly (see docs/08-runtime-bus.md).
class EventBus {
public:
    template <typename T>
    using Handler = std::function<void(const T&)>;

    template <typename T>
    ListenerId subscribe(Handler<T> fn) {
        std::lock_guard<std::mutex> g(mu_);
        ListenerId id = next_id_++;
        auto key = key_of<T>();
        auto& slot = typed_[key];
        if (!slot) slot = std::make_shared<TypedChannel<T>>();
        static_cast<TypedChannel<T>*>(slot.get())->handlers.emplace_back(id, std::move(fn));
        return id;
    }

    template <typename T>
    void unsubscribe(ListenerId id) {
        std::lock_guard<std::mutex> g(mu_);
        auto it = typed_.find(key_of<T>());
        if (it == typed_.end() || !it->second) return;
        auto* ch = static_cast<TypedChannel<T>*>(it->second.get());
        auto& v = ch->handlers;
        for (auto i = v.begin(); i != v.end(); ++i)
            if (i->first == id) { v.erase(i); break; }
    }

    template <typename T>
    std::size_t publish(const T& evt) {
        std::vector<std::pair<ListenerId, Handler<T>>> snapshot;
        {
            std::lock_guard<std::mutex> g(mu_);
            auto it = typed_.find(key_of<T>());
            if (it == typed_.end() || !it->second) { ++stats_dropped_; return 0; }
            snapshot = static_cast<TypedChannel<T>*>(it->second.get())->handlers;
        }
        for (auto& h : snapshot) h.second(evt);
        std::lock_guard<std::mutex> g(mu_);
        ++stats_published_;
        return snapshot.size();
    }

    [[nodiscard]] std::size_t listeners(EventName name) const {
        std::lock_guard<std::mutex> g(mu_);
        auto it = named_counts_.find(name);
        return it == named_counts_.end() ? 0 : it->second;
    }
    [[nodiscard]] u64 published() const { return stats_published_; }
    [[nodiscard]] u64 dropped()   const { return stats_dropped_; }
    void clear() { std::lock_guard<std::mutex> g(mu_); typed_.clear(); named_counts_.clear(); }

private:
    template <typename T> struct TypedChannel {
        std::vector<std::pair<ListenerId, Handler<T>>> handlers;
    };
    template <typename T> static std::size_t key_of() {
        return static_cast<std::size_t>(typeid(T).hash_code());
    }

    mutable std::mutex mu_;
    std::unordered_map<std::size_t, std::shared_ptr<void>> typed_;
    std::unordered_map<EventName, std::size_t> named_counts_;
    u64 next_id_ = 1, stats_published_ = 0, stats_dropped_ = 0;
};

// ---------------------------------------------------------------- events ----
struct TickEvent        { f32 dt = 0; f32 time = 0; u64 frame = 0; };
struct FixedTickEvent   { f32 fixed_dt = 0; u64 step = 0; };
struct SceneLoadedEvent { std::string scene; };
struct EntitySpawned    { EntityId entity; std::string prefab; };
struct EntityDestroyed  { EntityId entity; };
struct CollisionEvent   { EntityId a, b; f32 impulse = 0; f32 normal_x = 0, normal_y = 0; };
struct TouchBegan       { i32 id = 0; f32 x = 0, y = 0; f32 pressure = 1; };
struct TouchMoved       { i32 id = 0; f32 x = 0, y = 0; };
struct TouchEnded       { i32 id = 0; f32 x = 0, y = 0; };
struct GestureEvent     { std::string kind; f32 x = 0, y = 0, dx = 0, dy = 0, scale = 1, rotation = 0; };
struct GamepadEvent     { std::string button; bool down = false; i32 pad = 0; };
struct QualityChanged   { std::string tier; f32 render_scale = 1.0f; };
struct ThermalEvent     { i32 status = 0; f32 battery = 1.0f; };
struct SaveGameEvent    { std::string slot; bool ok = false; };
struct LoadGameEvent    { std::string slot; bool ok = false; };
struct IapUnlockEvent   { std::string sku; bool ok = false; };
struct AchievementEvent { std::string id; };
struct ScreenResized    { i32 width = 0, height = 0; };
struct AppLifecycle     { std::string state; };   // resume / pause / focus / blur

} // namespace prism
