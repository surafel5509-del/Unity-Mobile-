// =====================================================================
//  PRISM ENGINE — ecs/ecs.h
//  Sparse-set ECS: contiguous component arrays (data-oriented), O(1)
//  add/get/remove, bitmask-free iteration via multi-pool queries.
// =====================================================================
#pragma once
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <tuple>
#include <cstring>
#include <utility>
#include <memory>
#include <functional>
#include <algorithm>
#include <string>
#include "../core/types.h"
#include "../math/math.h"
#include "../core/event.h"

namespace prism {

/// Transform — the one component the engine guarantees on every entity.
struct Transform {
    math::Vec3 position{0, 0, 0};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
    EntityId   parent;
    math::Mat4 local_to_world() const {
        return math::Mat4::translate(position) * rotation.to_mat4() * math::Mat4::scale(scale);
    }
};

struct Tag { std::string value; };                  // e.g. "player", "enemy"
struct Sprite { std::string texture; math::Rect src; math::Color tint{1,1,1,1}; i32 order = 0; bool flip_x = false, flip_y = false; };
struct MeshRenderer { std::string mesh; std::string material; bool cast_shadow = true; };
struct Camera { f32 fov = 60.0f * math::Deg2Rad; f32 near_plane = 0.1f, far_plane = 100.0f; bool orthographic = false; f32 ortho_size = 10.0f; i32 depth = 0; };
struct Light { enum class Kind : u8 { Directional, Point, Spot } kind = Kind::Point; math::Color color{1,1,1,1}; f32 intensity = 1.0f, range = 10.0f; };
struct Rigidbody2D { math::Vec2 velocity{0,0}; f32 angular_velocity = 0, mass = 1.0f, inv_mass = 1.0f, restitution = 0.2f, friction = 0.3f, gravity_scale = 1.0f; bool kinematic = false, sleeping = false; };
struct Collider2D { enum class Shape : u8 { Box, Circle, Capsule, Polygon } shape = Shape::Box; math::Vec2 half_extents{0.5f, 0.5f}; f32 radius = 0.5f; bool is_trigger = false; i32 layer = 0; };
struct Health { f32 current = 100.0f, max = 100.0f; };
struct ScriptBinding { std::string class_name; u32 script_handle = kInvalidId; std::unordered_map<std::string, double> fields; };
struct AudioSource { std::string clip; f32 volume = 1.0f, pitch = 1.0f; bool loop = false, spatial = false, playing = false; };
struct Agent { f32 speed = 3.0f, radius = 0.5f; i32 nav_target = -1; math::Vec3 steering{0,0,0}; };
struct UiElement { enum class Kind : u8 { Panel, Button, Text, Image, Joystick } kind = Kind::Panel; math::Rect rect; std::string text; math::Color color{1,1,1,1}; bool interactable = true; };

class World;

// ------------------------------------------------------------------ pools --
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(std::size_t index) = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
};

/// Sparse set: dense array of components + dense array of entity indices.
template <typename T>
class ComponentPool : public IComponentPool {
public:
    T* add(std::size_t entity, const T& value = T{}) {
        auto it = sparse_.find(entity);
        if (it != sparse_.end()) { dense_[it->second] = value; return &dense_[it->second]; }
        sparse_[entity] = dense_.size();
        dense_.push_back(value);
        entities_.push_back(entity);
        return &dense_.back();
    }
    [[nodiscard]] T* get(std::size_t entity) {
        auto it = sparse_.find(entity);
        return it == sparse_.end() ? nullptr : &dense_[it->second];
    }
    [[nodiscard]] const T* get(std::size_t entity) const {
        auto it = sparse_.find(entity);
        return it == sparse_.end() ? nullptr : &dense_[it->second];
    }
    bool has(std::size_t entity) const { return sparse_.count(entity) > 0; }
    void remove(std::size_t entity) override {
        auto it = sparse_.find(entity);
        if (it == sparse_.end()) return;
        std::size_t idx = it->second, last = dense_.size() - 1;
        if (idx != last) {
            dense_[idx] = dense_[last];
            entities_[idx] = entities_[last];
            sparse_[entities_[idx]] = idx;
        }
        dense_.pop_back();
        entities_.pop_back();
        sparse_.erase(it);
    }
    [[nodiscard]] std::size_t size() const override { return dense_.size(); }
    [[nodiscard]] std::vector<T>&       data()       { return dense_; }
    [[nodiscard]] const std::vector<T>& data() const { return dense_; }
    [[nodiscard]] const std::vector<std::size_t>& entities() const { return entities_; }

private:
    std::unordered_map<std::size_t, std::size_t> sparse_;
    std::vector<T> dense_;
    std::vector<std::size_t> entities_;
};

// ---------------------------------------------------------------- systems --
class System {
public:
    virtual ~System() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual i32 order() const { return 100; }
    virtual void update(World& world, f32 dt) = 0;
    [[nodiscard]] bool enabled() const { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }
private:
    bool enabled_ = true;
};

/// Lambda system — the fast path for gameplay code and PrismScript systems.
class FnSystem : public System {
public:
    FnSystem(std::string n, std::function<void(World&, f32)> fn, i32 ord = 100)
        : name_(std::move(n)), fn_(std::move(fn)), order_(ord) {}
    [[nodiscard]] std::string name() const override { return name_; }
    [[nodiscard]] i32 order() const override { return order_; }
    void update(World& w, f32 dt) override { if (fn_) fn_(w, dt); }
private:
    std::string name_;
    std::function<void(World&, f32)> fn_;
    i32 order_;
};

// ------------------------------------------------------------------ world --
class World {
public:
    explicit World(EventBus* bus = nullptr) : bus_(bus) {}

    EntityId create(const std::string& prefab = {}) {
        EntityId e = alloc();
        transforms_.add(e.index, Transform{});
        if (!prefab.empty()) tags_.add(e.index, Tag{prefab});
        if (bus_) bus_->publish(EntitySpawned{e, prefab});
        return e;
    }

    void destroy(EntityId e) {
        if (!alive(e)) return;
        for (auto& kv : pools_) kv.second->remove(e.index);
        tags_.remove(e.index);
        transforms_.remove(e.index);
        ++generations_[e.index];
        free_.push_back(e.index);
        if (bus_) bus_->publish(EntityDestroyed{e});
    }

    [[nodiscard]] bool alive(EntityId e) const {
        return e.index < generations_.size() && generations_[e.index] == e.generation;
    }

    template <typename T>
    T& add(EntityId e, const T& c = T{}) {
        auto it = pools_.find(std::type_index(typeid(T)));
        ComponentPool<T>* pool;
        if (it == pools_.end()) {
            auto p = std::make_unique<ComponentPool<T>>();
            pool = p.get();
            pools_[std::type_index(typeid(T))] = std::move(p);
        } else {
            pool = static_cast<ComponentPool<T>*>(it->second.get());
        }
        return *pool->add(e.index, c);
    }

    template <typename T>
    [[nodiscard]] T* get(EntityId e) {
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it == pools_.end()) return nullptr;
        return static_cast<ComponentPool<T>*>(it->second.get())->get(e.index);
    }

    template <typename T>
    bool has(EntityId e) {
        auto it = pools_.find(std::type_index(typeid(T)));
        return it != pools_.end() && static_cast<ComponentPool<T>*>(it->second.get())->has(e.index);
    }

    template <typename T>
    void remove(EntityId e) {
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it != pools_.end()) it->second->remove(e.index);
    }

    template <typename T>
    [[nodiscard]] ComponentPool<T>* pool() {
        auto it = pools_.find(std::type_index(typeid(T)));
        return it == pools_.end() ? nullptr : static_cast<ComponentPool<T>*>(it->second.get());
    }

    /// Iterate the smallest matching pool, filtering by the remaining components.
    template <typename... Ts, typename Fn>
    std::size_t each(Fn&& fn) {
        static_assert(sizeof...(Ts) >= 1, "each<> needs at least one component type");
        return each_impl<std::tuple<Ts...>>(std::forward<Fn>(fn),
                                            std::index_sequence_for<Ts...>{});
    }

    Transform&       transform(EntityId e)       { return *transforms_.get(e.index); }
    [[nodiscard]] const Transform* transform(EntityId e) const { return transforms_.get(e.index); }
    [[nodiscard]] ComponentPool<Transform>& transforms() { return transforms_; }
    [[nodiscard]] std::size_t entity_count() const { return transforms_.size(); }
    [[nodiscard]] std::size_t system_count() const { return systems_.size(); }

    void add_system(std::unique_ptr<System> s) {
        systems_.push_back(std::move(s));
        std::stable_sort(systems_.begin(), systems_.end(),
                         [](const std::unique_ptr<System>& a, const std::unique_ptr<System>& b) {
                             return a->order() < b->order();
                         });
    }
    void update(f32 dt) { for (auto& s : systems_) if (s->enabled()) s->update(*this, dt); }

    /// Snapshot used by rollback netcode / save games / determinism tests.
    struct Snapshot {
        u64 frame = 0;
        std::vector<u32> hashes;    // per-entity transform hash
        u32 checksum = 0;
    };
    Snapshot snapshot(u64 frame) const {
        Snapshot s; s.frame = frame;
        u32 acc = 2166136261u;
        for (std::size_t i = 0; i < transforms_.size(); ++i) {
            const auto& t = transforms_.data()[i];
            u32 h = 0;
            auto mix = [&](f32 v) {
                u32 bits; std::memcpy(&bits, &v, sizeof(bits));
                h = (h ^ bits) * 16777619u;
            };
            mix(t.position.x); mix(t.position.y); mix(t.position.z);
            mix(t.rotation.x); mix(t.rotation.y); mix(t.rotation.z); mix(t.rotation.w);
            s.hashes.push_back(h);
            acc = (acc ^ h) * 16777619u;
        }
        s.checksum = acc;
        return s;
    }

private:
    template <typename Tuple, typename Fn, std::size_t... Is>
    std::size_t each_impl(Fn&& fn, std::index_sequence<Is...>) {
        using First = std::tuple_element_t<0, Tuple>;
        ComponentPool<First>* first = pool<First>();
        if (!first || !((pool<std::tuple_element_t<Is, Tuple>>() != nullptr) && ...)) return 0;
        std::size_t count = 0;
        // copy entity list: fn() may add/remove components
        std::vector<std::size_t> ids = first->entities();
        for (std::size_t idx : ids) {
            auto* t = transforms_.get(idx);
            if (!t) continue;
            if (!(pool<std::tuple_element_t<Is, Tuple>>()->has(idx) && ...)) continue;
            EntityId e{static_cast<u32>(idx), generations_[idx]};
            fn(e, *t, *pool<std::tuple_element_t<Is, Tuple>>()->get(idx)...);
            ++count;
        }
        return count;
    }

    EntityId alloc() {
        u32 idx;
        if (!free_.empty()) { idx = free_.back(); free_.pop_back(); }
        else { idx = static_cast<u32>(generations_.size()); generations_.push_back(0); }
        return EntityId{idx, generations_[idx]};
    }

    EventBus* bus_;
    ComponentPool<Transform> transforms_;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
    ComponentPool<Tag> tags_;
    std::vector<u32> generations_;
    std::vector<u32> free_;
    std::vector<std::unique_ptr<System>> systems_;
};

} // namespace prism
