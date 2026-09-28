// =====================================================================
//  PRISM ENGINE — particles/particles.h
//  A deterministic CPU particle system shaped like the node/Cascade-style
//  editors: an emitter shape, emission rate, and modules applied over the
//  particle's life (gravity, turbulence, size & colour curves, ground
//  bounce). The simulation is pure math with a seeded RNG, so two systems
//  with the same seed produce bit-identical results (rollback-friendly)
//  and the renderer just consumes `instances()`.
// =====================================================================
#pragma once
#include <cstdint>
#include <vector>
#include "../core/engine.h"      // Rng
#include "../core/types.h"
#include "../math/math.h"

namespace prism::particles {

// ------------------------------------------------------------- curves -----
struct FloatStop { f32 t; f32 v; };
struct ColorStop { f32 t; math::Color c; };
[[nodiscard]] f32 eval_curve(const std::vector<FloatStop>& stops, f32 t, f32 fallback);
[[nodiscard]] math::Color eval_gradient(const std::vector<ColorStop>& stops, f32 t, math::Color fallback);

// ------------------------------------------------------------- emitter ----
struct EmitterShape {
    enum class Kind : u8 { Point, Sphere, Box, Cone } kind = Kind::Point;
    math::Vec3 extents = math::Vec3(1, 1, 1);      // Box half extents
    f32 radius = 0.5f;                              // Sphere
    f32 cone_half_angle = 0.5f;                     // Cone, radians
    math::Vec3 cone_dir = math::Vec3(0, 1, 0);
    void sample(Rng& rng, math::Vec3& out_pos, math::Vec3& out_dir) const;
};

// -------------------------------------------------------------- config ----
struct ParticleConfig {
    i32 capacity = 512;
    f32 rate = 100.0f;                  // particles per second
    f32 life = 1.5f;
    f32 life_randomness = 0.0f;         // 0..1 fraction of life
    math::Vec3 velocity = math::Vec3(0, 2, 0);
    f32 speed_randomness = 0.0f;
    math::Vec3 gravity = math::Vec3(0, -9.81f, 0);
    EmitterShape shape;
    std::vector<FloatStop> size_over_life;      // multiplied on base size
    std::vector<ColorStop> color_over_life;
    f32 base_size = 0.1f;
    f32 spin = 0.0f;                            // radians per second
    f32 turbulence = 0.0f;                      // accel amplitude
    f32 turbulence_frequency = 2.0f;
    bool collide_ground = false;
    f32 ground_y = 0.0f;
    f32 restitution = 0.4f;
};

struct Particle {
    math::Vec3 position, velocity;
    f32 age = 0, life = 1, size = 1, rotation = 0;
    u32 seed = 0;
    bool alive = false;
};

struct ParticleInstance {
    math::Vec3 position;
    f32 size = 0;
    f32 rotation = 0;
    math::Color color = math::Color{1, 1, 1, 1};
};

// -------------------------------------------------------------- system ----
class ParticleSystem {
public:
    ParticleSystem() = default;
    explicit ParticleSystem(const ParticleConfig& c, u32 seed = 1);

    void update(f32 dt);
    void burst(i32 count);
    void set_rate(f32 r) { cfg_.rate = r; }
    void set_emitting(bool e) { emitting_ = e; }

    [[nodiscard]] i32 alive_count() const { return alive_; }
    [[nodiscard]] u64 total_spawned() const { return spawned_; }
    [[nodiscard]] f32 time() const { return time_; }
    [[nodiscard]] const ParticleConfig& config() const { return cfg_; }
    [[nodiscard]] const std::vector<Particle>& particles() const { return parts_; }

    /// Render-ready snapshot of the living particles.
    void instances(std::vector<ParticleInstance>& out) const;
    [[nodiscard]] bool bounds(math::Vec3& out_min, math::Vec3& out_max) const;
    /// Order-independent hash of the live state; equal for equal simulations.
    [[nodiscard]] u32 checksum() const;

private:
    void spawn_one();

    ParticleConfig cfg_;
    std::vector<Particle> parts_;
    Rng rng_;
    f32 emit_acc_ = 0, time_ = 0;
    i32 alive_ = 0;
    u64 spawned_ = 0;
    bool emitting_ = true;
};

} // namespace prism::particles
