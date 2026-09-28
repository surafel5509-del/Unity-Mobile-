// =====================================================================
//  PRISM ENGINE — physics2d/physics2d.h
//  Box2D-style sequential-impulse solver: dynamic AABB broadphase,
//  SAT narrowphase (box/box, circle/circle, box/circle), friction +
//  restitution, distance & spring joints, triggers, sleeping, CCD.
//  Deterministic mode: fixed iteration counts + no unordered iteration.
// =====================================================================
#pragma once
#include <vector>
#include <memory>
#include <string>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::physics2d {

using BodyId = u32;
inline constexpr BodyId kNullBody = 0xFFFFFFFFu;

enum class BodyType : u8 { Static, Kinematic, Dynamic };
enum class ShapeType : u8 { Circle, Box, Capsule, Polygon };

struct AABB {
    math::Vec2 min{0, 0}, max{0, 0};
    [[nodiscard]] bool overlaps(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y;
    }
    [[nodiscard]] math::Vec2 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] math::Vec2 extents() const { return (max - min) * 0.5f; }
    void expand(f32 m) { min -= math::Vec2{m, m}; max += math::Vec2{m, m}; }
    void merge(const AABB& o) {
        min = math::min(min, o.min);
        max = math::max(max, o.max);
    }
};

struct Shape {
    ShapeType type = ShapeType::Box;
    math::Vec2 half_extents{0.5f, 0.5f};   // box
    f32 radius = 0.5f;                      // circle / capsule
    f32 capsule_half_length = 0.5f;
    std::vector<math::Vec2> polygon;        // convex, CCW
    f32 density = 1.0f;
    f32 friction = 0.3f;
    f32 restitution = 0.2f;
    bool sensor = false;                     // trigger
    u32 layer = 1, mask = 0xFFFFFFFFu;
};

struct Body {
    BodyId id = kNullBody;
    BodyType type = BodyType::Dynamic;
    math::Vec2 position;
    f32 angle = 0;
    math::Vec2 linear_velocity;
    f32 angular_velocity = 0;
    math::Vec2 force;
    f32 torque = 0;
    f32 mass = 1, inv_mass = 1, inertia = 1, inv_inertia = 1;
    f32 linear_damping = 0.05f, angular_damping = 0.05f;
    f32 gravity_scale = 1.0f;
    bool fixed_rotation = false;
    bool sleeping = false;
    bool continuous = false;                 // CCD (bullets)
    bool allow_sleep = true;
    f32 sleep_timer = 0;
    std::vector<Shape> shapes;
    u64 user_data = 0;                       // ECS entity index
    std::string tag;

    void set_mass_from_shapes();
    [[nodiscard]] AABB aabb() const;
    math::Vec2 world_point(const math::Vec2& local) const;
    math::Vec2 velocity_at(const math::Vec2& world_point) const;
    void apply_impulse(const math::Vec2& impulse, const math::Vec2& point);
    void wake() { sleeping = false; sleep_timer = 0; }
};

struct Joint {
    enum class Kind : u8 { Distance, Spring, Revolute } kind = Kind::Distance;
    BodyId a = kNullBody, b = kNullBody;
    math::Vec2 anchor_a, anchor_b;
    f32 length = 1.0f;          // distance
    f32 stiffness = 50.0f;      // spring
    f32 damping = 5.0f;
    f32 min_length = 0.0f, max_length = 1e9f;
    bool collide_connected = false;
};

struct Contact {
    BodyId a = kNullBody, b = kNullBody;
    math::Vec2 normal;          // from a to b
    math::Vec2 point;
    f32 penetration = 0;
    f32 impulse = 0;            // accumulated normal impulse (for events)
    bool sensor = false;
    bool started = false, ended = false;
};

struct PhysicsConfig {
    math::Vec2 gravity{0, -9.81f};
    i32 velocity_iterations = 8;
    i32 position_iterations = 3;
    f32 fixed_dt = 1.0f / 60.0f;
    f32 linear_sleep_threshold = 0.05f;
    f32 angular_sleep_threshold = 0.1f;
    f32 sleep_delay = 0.6f;
    f32 max_translation_per_step = 8.0f;    // CCD clamp
    bool deterministic = false;
    bool continuous = true;
};

struct PhysicsStats {
    u64 steps = 0, contacts = 0, pairs_tested = 0, pairs_collided = 0;
    i32 bodies = 0, awake = 0, joints = 0;
    f64 step_ms = 0;
};

class World {
public:
    explicit World(PhysicsConfig cfg = {}) : cfg_(cfg) {}

    BodyId create_body(const Body& b);
    void destroy_body(BodyId id);
    Body* body(BodyId id);
    [[nodiscard]] const Body* body(BodyId id) const;
    [[nodiscard]] const std::vector<std::unique_ptr<Body>>& bodies() const { return bodies_; }

    u32 add_joint(const Joint& j);
    void remove_joint(u32 id);
    Joint* joint(u32 id);

    /// Advance the simulation by dt using fixed sub-steps.
    void step(f32 dt);

    /// Queries
    [[nodiscard]] BodyId raycast(math::Vec2 from, math::Vec2 to, bool sensors = false) const;
    std::vector<BodyId> overlap_aabb(const AABB& box) const;
    [[nodiscard]] const std::vector<Contact>& contacts() const { return contacts_; }
    [[nodiscard]] const PhysicsStats& stats() const { return stats_; }
    [[nodiscard]] const PhysicsConfig& config() const { return cfg_; }
    void set_config(const PhysicsConfig& c) { cfg_ = c; }

    /// Layer collision matrix (32 layers).
    void set_layer_collision(u32 layer_a, u32 layer_b, bool enabled);
    [[nodiscard]] bool layers_collide(u32 a, u32 b) const;

    /// Deterministic state hash — used by LAN rollback netcode.
    [[nodiscard]] u64 checksum() const;

private:
    void broadphase(std::vector<std::pair<BodyId, BodyId>>& pairs) const;
    bool narrowphase(Body& a, Body& b, Contact& out) const;
    bool box_box(const Body& a, const Shape& sa, const Body& b, const Shape& sb, Contact& out) const;
    bool circle_circle(const Body& a, const Shape& sa, const Body& b, const Shape& sb, Contact& out) const;
    bool box_circle(const Body& a, const Shape& sa, const Body& b, const Shape& sb, Contact& out) const;
    void solve_contacts();
    void integrate(f32 dt);
    void sleep_pass(f32 dt);

    PhysicsConfig cfg_;
    std::vector<std::unique_ptr<Body>> bodies_;
    std::vector<BodyId> free_ids_;
    std::vector<Joint> joints_;
    std::vector<Contact> contacts_;
    std::vector<Contact> prev_contacts_;
    std::vector<std::pair<BodyId, BodyId>> pairs_;
    std::vector<bool> layer_matrix_;   // 32x32
    PhysicsStats stats_;
    f32 time_left_ = 0;
};

} // namespace prism::physics2d
