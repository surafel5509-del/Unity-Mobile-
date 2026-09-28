// =====================================================================
//  PRISM ENGINE — physics3d/physics3d.h
//  Rigid-body dynamics for the 3D path: sphere / box / capsule colliders,
//  AABB broadphase, impulse resolution with restitution, friction and
//  positional correction, sleeping, and raycasting. Collisions treat
//  boxes as axis-aligned (rotation is carried for rendering but does not
//  enter the narrowphase) — the same simplification a mobile first pass
//  ships with, and it keeps the solver deterministic.
// =====================================================================
#pragma once
#include <cstdint>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::physics3d {

enum class ShapeType : u8 { Sphere, Box, Capsule };
const char* shape_name(ShapeType t);

struct Collider {
    ShapeType type = ShapeType::Sphere;
    f32 radius = 0.5f;                          // Sphere, Capsule
    math::Vec3 half = math::Vec3(0.5f, 0.5f, 0.5f);   // Box
    f32 capsule_half_height = 0.5f;             // Capsule, along Y
    /// Conservative AABB half extents around the origin.
    [[nodiscard]] math::Vec3 aabb_half() const;
};

struct BodyConfig {
    math::Vec3 position;
    math::Vec3 velocity;
    Collider collider;
    f32 mass = 1.0f;                            // ignored when is_static
    f32 restitution = 0.0f;
    f32 friction = 0.5f;
    bool is_static = false;
};

struct Body {
    u32 id = 0;
    math::Vec3 position, velocity;
    math::Quat rotation;                        // rendering only
    Collider collider;
    f32 mass = 1, inv_mass = 1;
    f32 restitution = 0, friction = 0.5f;
    bool is_static = false;
    bool sleeping = false;
    f32 sleep_timer = 0;
};

struct Contact {
    u32 a = 0, b = 0;
    math::Vec3 point;
    math::Vec3 normal;                          // from a toward b
    f32 penetration = 0;
};

struct RayHit {
    u32 body = 0;
    math::Vec3 point;
    math::Vec3 normal;
    f32 distance = 0;
};

class World3D {
public:
    struct Config {
        math::Vec3 gravity = math::Vec3(0, -9.81f, 0);
        i32 velocity_iterations = 6;
        f32 sleep_speed = 0.08f;                // below this a body may sleep
        f32 sleep_time = 0.6f;                  // seconds below threshold
        f32 positional_percent = 0.8f;
        f32 positional_slop = 0.005f;
    };

    World3D();
    explicit World3D(const Config& c);

    u32 add(const BodyConfig& bc);
    void remove(u32 id);
    [[nodiscard]] Body* get(u32 id);
    [[nodiscard]] const Body* get(u32 id) const;
    [[nodiscard]] std::size_t body_count() const { return bodies_.size(); }
    [[nodiscard]] std::size_t awake_count() const;
    void wake(u32 id);

    /// One fixed simulation step.
    void step(f32 dt);
    [[nodiscard]] const std::vector<Contact>& contacts() const { return contacts_; }
    [[nodiscard]] bool raycast(math::Vec3 from, math::Vec3 dir, f32 max_distance, RayHit& out) const;
    [[nodiscard]] const Config& config() const { return cfg_; }

    /// Narrowphase, exposed so tests can verify contact geometry directly.
    [[nodiscard]] static bool collide(const Body& a, const Body& b, Contact& out);

private:
    void integrate(f32 dt);
    void solve();

    Config cfg_;
    std::vector<Body> bodies_;
    std::vector<Contact> contacts_;
    u32 next_id_ = 1;
};

} // namespace prism::physics3d
