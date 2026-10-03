// =====================================================================
//  PRISM ENGINE — tests/test_particles.cpp
//  Curves/gradients, emitter shapes, rate + lifetime pooling, gravity with
//  ground bounce, and seeded determinism.
// =====================================================================
#include "prism_test.h"
#include "prism/particles/particles.h"

#include <cmath>

using namespace prism;
using namespace prism::particles;

namespace {
bool close(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
} // namespace

PRISM_TEST(particles_curves_and_gradients_interpolate) {
    std::vector<FloatStop> stops{{0.0f, 0.0f}, {1.0f, 10.0f}};
    PRISM_CHECK(close(eval_curve(stops, 0.25f, 0), 2.5f));
    PRISM_CHECK(close(eval_curve(stops, -1.0f, 0), 0.0f));       // clamped low
    PRISM_CHECK(close(eval_curve(stops, 5.0f, 0), 10.0f));       // clamped high
    PRISM_CHECK(close(eval_curve({}, 0.5f, 7.0f), 7.0f));        // fallback

    std::vector<ColorStop> g{{0.0f, math::Color{0, 0, 0, 1}}, {1.0f, math::Color{1, 1, 1, 0}}};
    const math::Color c = eval_gradient(g, 0.5f, math::Color{0, 0, 0, 0});
    PRISM_CHECK(close(c.r, 0.5f) && close(c.a, 0.5f));
    const math::Color fb = eval_gradient({}, 0.5f, math::Color{9, 0, 0, 0});
    PRISM_CHECK(close(fb.r, 9.0f));
}

PRISM_TEST(particles_emitter_shapes_sample_their_volume) {
    Rng rng(42);
    EmitterShape point;
    math::Vec3 p, d;
    for (int i = 0; i < 20; ++i) {
        point.sample(rng, p, d);
        PRISM_CHECK(close(p.length(), 0.0f));
    }

    EmitterShape sphere; sphere.kind = EmitterShape::Kind::Sphere; sphere.radius = 2.0f;
    for (int i = 0; i < 200; ++i) {
        sphere.sample(rng, p, d);
        PRISM_CHECK(p.length() <= 2.0f + 1e-4f);
        PRISM_CHECK(close(d.length(), 1.0f, 1e-3f));
    }

    EmitterShape box; box.kind = EmitterShape::Kind::Box; box.extents = math::Vec3(1, 2, 3);
    for (int i = 0; i < 200; ++i) {
        box.sample(rng, p, d);
        PRISM_CHECK(std::fabs(p.x) <= 1.0f + 1e-4f);
        PRISM_CHECK(std::fabs(p.y) <= 2.0f + 1e-4f);
        PRISM_CHECK(std::fabs(p.z) <= 3.0f + 1e-4f);
    }

    EmitterShape cone; cone.kind = EmitterShape::Kind::Cone;
    cone.cone_half_angle = math::Pi * 0.25f;
    f32 max_angle = 0;
    for (int i = 0; i < 200; ++i) {
        cone.sample(rng, p, d);
        const f32 ang = std::acos(d.dot(math::Vec3(0, 1, 0)));
        max_angle = std::max(max_angle, ang);
    }
    PRISM_CHECK(max_angle <= math::Pi * 0.25f + 1e-4f);
}

PRISM_TEST(particles_rate_lifetime_and_pool_cap) {
    ParticleConfig cfg;
    cfg.rate = 10;
    cfg.life = 0.5f;
    cfg.velocity = math::Vec3(0, 1, 0);
    cfg.gravity = math::Vec3();
    cfg.capacity = 4;
    ParticleSystem sys(cfg, 7);
    for (int i = 0; i < 60; ++i) sys.update(1.0f / 60.0f);   // 1 second, 10 spawns wanted
    PRISM_CHECK(sys.alive_count() <= 4);                           // pool cap respected
    PRISM_CHECK(sys.total_spawned() >= 8);                         // ~10 emitted
    for (int i = 0; i < 200; ++i) sys.update(1.0f / 60.0f);  // drain with emitting off
    sys.set_emitting(false);
    for (int i = 0; i < 200; ++i) sys.update(1.0f / 60.0f);
    PRISM_CHECK_EQ(sys.alive_count(), 0);
}

PRISM_TEST(particles_gravity_bounce_and_instances) {
    ParticleConfig cfg;
    cfg.rate = 0;
    cfg.life = 10.0f;
    cfg.capacity = 8;
    cfg.gravity = math::Vec3(0, -10.0f, 0);
    cfg.velocity = math::Vec3(0, 5, 0);
    cfg.collide_ground = true;
    cfg.ground_y = 0.0f;
    cfg.restitution = 0.5f;
    cfg.base_size = 0.2f;
    cfg.size_over_life = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    ParticleSystem sys(cfg, 3);
    sys.burst(1);
    PRISM_CHECK_EQ(sys.alive_count(), 1);

    sys.update(0.5f);                                        // apex at t=0.5, back to 0 at t=1
    sys.update(0.6f);                                        // bounced: vy = -5*0.5 = 2.5 up
    PRISM_CHECK(sys.particles()[0].position.y >= 0.0f);
    PRISM_CHECK(sys.particles()[0].velocity.y > 0.0f);           // reflected upward

    std::vector<ParticleInstance> insts;
    sys.instances(insts);
    PRISM_CHECK_EQ(insts.size(), std::size_t(1));
    PRISM_CHECK(insts[0].size > 0.0f && insts[0].size < 0.2f);   // shrinking over life
    math::Vec3 mn, mx;
    PRISM_CHECK(sys.bounds(mn, mx));
    PRISM_CHECK(close(mn.y, mx.y));                          // single particle
}

PRISM_TEST(particles_determinism_same_seed_divergence_different) {
    ParticleConfig cfg;
    cfg.rate = 50;
    cfg.life = 2.0f;
    cfg.velocity = math::Vec3(1, 3, 0);
    cfg.gravity = math::Vec3(0, -9.81f, 0);
    cfg.speed_randomness = 0.5f;
    cfg.life_randomness = 0.4f;
    cfg.turbulence = 2.0f;
    ParticleSystem a(cfg, 1234);
    ParticleSystem b(cfg, 1234);
    ParticleSystem c(cfg, 4321);
    for (int i = 0; i < 120; ++i) { a.update(1.0f / 60.0f); b.update(1.0f / 60.0f); c.update(1.0f / 60.0f); }
    PRISM_CHECK_EQ(a.checksum(), b.checksum());
    PRISM_CHECK(a.checksum() != c.checksum());
    PRISM_CHECK_EQ(a.total_spawned(), b.total_spawned());
}
