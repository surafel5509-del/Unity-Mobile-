// =====================================================================
//  PRISM ENGINE — tests/test_physics3d.cpp
//  3D rigid bodies: resting contacts, impulse exchange, momentum, stacking,
//  sleeping, raycasts and determinism.
// =====================================================================
#include "prism_test.h"
#include "prism/physics3d/physics3d.h"

#include <cmath>

using namespace prism;
using namespace prism::physics3d;

namespace {
bool close(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
u32 ground_box(World3D& w, f32 top_y = 0.0f, f32 half = 5.0f) {
    BodyConfig g;
    g.is_static = true;
    g.position = math::Vec3(0, top_y - half, 0);
    g.collider.type = ShapeType::Box;
    g.collider.half = math::Vec3(half, half, half);
    return w.add(g);
}
} // namespace

PRISM_TEST(p3d_shape_names_and_aabb_extents) {
    PRISM_CHECK(std::string(shape_name(ShapeType::Sphere)) == "sphere");
    PRISM_CHECK(std::string(shape_name(ShapeType::Box)) == "box");
    PRISM_CHECK(std::string(shape_name(ShapeType::Capsule)) == "capsule");
    Collider s; // sphere r=0.5 default
    PRISM_CHECK(close(s.aabb_half().y, 0.5f));
    Collider b; b.type = ShapeType::Box; b.half = math::Vec3(1, 2, 3);
    PRISM_CHECK(close(b.aabb_half().y, 2.0f));
    Collider c; c.type = ShapeType::Capsule; c.radius = 0.3f; c.capsule_half_height = 1.0f;
    PRISM_CHECK(close(c.aabb_half().y, 1.3f));
}

PRISM_TEST(p3d_narrowphase_contact_geometry) {
    Body a, b;
    a.id = 1; b.id = 2;
    a.position = math::Vec3(0, 0, 0); a.collider.radius = 0.5f;   // centre on the box top plane
    b.position = math::Vec3(0, -1, 0); b.collider.type = ShapeType::Box; b.collider.half = math::Vec3(2, 1, 2);
    Contact c;
    PRISM_CHECK(World3D::collide(a, b, c));
    PRISM_CHECK(close(c.penetration, 0.5f, 1e-3f));        // centre on surface -> full radius
    PRISM_CHECK(close(c.normal.y, -1.0f, 1e-3f));          // a -> b points down
    a.position.y = 2.0f;
    PRISM_CHECK(!World3D::collide(a, b, c));               // separated
}

PRISM_TEST(p3d_falling_sphere_rests_and_sleeps) {
    World3D w;
    ground_box(w);
    BodyConfig s;
    s.position = math::Vec3(0, 3, 0);
    s.restitution = 0.0f;
    const u32 id = w.add(s);
    for (int i = 0; i < 180; ++i) w.step(1.0f / 60.0f);
    const Body* b = w.get(id);
    PRISM_CHECK(b != nullptr);
    PRISM_CHECK(std::fabs(b->position.y - 0.5f) < 0.02f);  // ground top 0 + radius
    PRISM_CHECK(b->sleeping);
    PRISM_CHECK_EQ(w.awake_count(), std::size_t(0));
}

PRISM_TEST(p3d_elastic_head_on_collision_swaps_velocities) {
    World3D::Config cfg;
    cfg.gravity = math::Vec3();
    World3D w(cfg);
    BodyConfig a, b;
    a.position = math::Vec3(0, 0, -2); a.velocity = math::Vec3(0, 0, 5); a.restitution = 1.0f;
    b.position = math::Vec3(0, 0, 2);  b.velocity = math::Vec3(0, 0, -5); b.restitution = 1.0f;
    const u32 ia = w.add(a), ib = w.add(b);
    for (int i = 0; i < 60; ++i) w.step(1.0f / 60.0f);
    PRISM_CHECK(w.get(ia)->velocity.z < -4.0f);            // +5 in, -5 out (equal masses swap)
    PRISM_CHECK(w.get(ib)->velocity.z > 4.0f);             // -5 in, +5 out
}

PRISM_TEST(p3d_inelastic_collision_conserves_momentum) {
    World3D::Config cfg;
    cfg.gravity = math::Vec3();
    World3D w(cfg);
    BodyConfig a, b;
    a.mass = 1; a.position = math::Vec3(0, 0, -2); a.velocity = math::Vec3(0, 0, 4);
    b.mass = 3; b.position = math::Vec3(0, 0, 2);
    const u32 ia = w.add(a), ib = w.add(b);
    for (int i = 0; i < 90; ++i) w.step(1.0f / 60.0f);
    const f32 p = w.get(ia)->velocity.z * 1.0f + w.get(ib)->velocity.z * 3.0f;
    PRISM_CHECK(std::fabs(p - 4.0f) < 0.05f);              // momentum 1*4 + 3*0
    PRISM_CHECK(std::fabs(w.get(ia)->velocity.z - w.get(ib)->velocity.z) < 0.1f);
}

PRISM_TEST(p3d_box_stack_does_not_sink) {
    World3D w;
    ground_box(w, 0.0f, 1.0f);                             // top at y = 0
    BodyConfig box;
    box.position = math::Vec3(0, 1.0f, 0);
    box.collider.type = ShapeType::Box;
    box.collider.half = math::Vec3(0.5f, 0.5f, 0.5f);
    const u32 id = w.add(box);
    for (int i = 0; i < 180; ++i) w.step(1.0f / 60.0f);
    const Body* b = w.get(id);
    PRISM_CHECK(std::fabs(b->position.y - 0.5f) < 0.02f);  // rests exactly on the floor
    PRISM_CHECK(b->sleeping);
}

PRISM_TEST(p3d_capsule_rests_at_half_height_plus_radius) {
    World3D w;
    ground_box(w);
    BodyConfig cap;
    cap.position = math::Vec3(0, 5, 0);
    cap.collider.type = ShapeType::Capsule;
    cap.collider.radius = 0.3f;
    cap.collider.capsule_half_height = 1.0f;
    const u32 id = w.add(cap);
    for (int i = 0; i < 240; ++i) w.step(1.0f / 60.0f);
    PRISM_CHECK(std::fabs(w.get(id)->position.y - 1.3f) < 0.03f);
}

PRISM_TEST(p3d_raycast_hits_sphere_and_box) {
    World3D w;
    BodyConfig s;
    s.position = math::Vec3(0, 0, 0);
    s.collider.radius = 1.0f;
    const u32 sphere = w.add(s);
    BodyConfig b;
    b.position = math::Vec3(10, 0, 0);
    b.collider.type = ShapeType::Box;
    b.collider.half = math::Vec3(1, 1, 1);
    w.add(b);

    RayHit hit;
    PRISM_CHECK(w.raycast(math::Vec3(-5, 0, 0), math::Vec3(1, 0, 0), 20.0f, hit));
    PRISM_CHECK_EQ(hit.body, sphere);
    PRISM_CHECK(close(hit.distance, 4.0f, 1e-3f));
    PRISM_CHECK(close(hit.normal.x, -1.0f, 1e-3f));

    PRISM_CHECK(w.raycast(math::Vec3(10, 0, -5), math::Vec3(0, 0, 1), 20.0f, hit));
    PRISM_CHECK(close(hit.distance, 4.0f, 1e-3f));
    PRISM_CHECK(close(hit.point.z, -1.0f, 1e-3f));
    PRISM_CHECK(close(hit.normal.z, -1.0f, 1e-3f));

    PRISM_CHECK(!w.raycast(math::Vec3(0, 50, 0), math::Vec3(1, 0, 0), 20.0f, hit));
}

PRISM_TEST(p3d_simulation_is_deterministic) {
    auto build = [](World3D& w) {
        ground_box(w);
        BodyConfig s;
        s.position = math::Vec3(0, 4, 0);
        s.restitution = 0.4f;
        w.add(s);
        BodyConfig d;
        d.position = math::Vec3(0.1f, 6, 0.05f);
        d.velocity = math::Vec3(0.3f, 0, -0.2f);
        w.add(d);
    };
    World3D a, b;
    build(a);
    build(b);
    for (int i = 0; i < 300; ++i) { a.step(1.0f / 60.0f); b.step(1.0f / 60.0f); }
    for (std::size_t i = 0; i < a.body_count(); ++i) {
        PRISM_CHECK(close(a.get(static_cast<u32>(i + 1))->position.x, b.get(static_cast<u32>(i + 1))->position.x, 0));
        PRISM_CHECK(close(a.get(static_cast<u32>(i + 1))->position.y, b.get(static_cast<u32>(i + 1))->position.y, 0));
        PRISM_CHECK(close(a.get(static_cast<u32>(i + 1))->position.z, b.get(static_cast<u32>(i + 1))->position.z, 0));
    }
}
