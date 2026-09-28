// =====================================================================
//  PRISM ENGINE — physics3d/physics3d.cpp
// =====================================================================
#include "prism/physics3d/physics3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace prism::physics3d {

namespace {
inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr f32 kEps = 1e-6f;
} // namespace

const char* shape_name(ShapeType t) {
    switch (t) {
        case ShapeType::Sphere: return "sphere";
        case ShapeType::Box: return "box";
        case ShapeType::Capsule: return "capsule";
    }
    return "unknown";
}

math::Vec3 Collider::aabb_half() const {
    switch (type) {
        case ShapeType::Sphere: return math::Vec3(radius, radius, radius);
        case ShapeType::Box: return half;
        case ShapeType::Capsule: return math::Vec3(radius, capsule_half_height + radius, radius);
    }
    return math::Vec3(radius, radius, radius);
}

// ------------------------------------------------------------- narrowphase --
namespace {

bool sphere_sphere(const math::Vec3& pa, f32 ra, const math::Vec3& pb, f32 rb, Contact& out) {
    const math::Vec3 d = pb - pa;
    const f32 dist = d.length();
    const f32 pen = ra + rb - dist;
    if (pen <= 0.0f) return false;
    out.normal = dist > kEps ? d / dist : math::Vec3(0, 1, 0);   // a -> b
    out.penetration = pen;
    out.point = pa + out.normal * (ra - pen * 0.5f);
    return true;
}

/// Sphere `s` vs axis-aligned box `b`. `n_out` points from the sphere toward
/// the box (used directly when the sphere is body a).
bool sphere_box(const math::Vec3& sc, f32 r, const math::Vec3& bc, const math::Vec3& bh,
                Contact& out) {
    const math::Vec3 lo = bc - bh, hi = bc + bh;
    const bool inside = sc.x > lo.x && sc.x < hi.x && sc.y > lo.y && sc.y < hi.y && sc.z > lo.z && sc.z < hi.z;
    const math::Vec3 c(clampf(sc.x, lo.x, hi.x), clampf(sc.y, lo.y, hi.y), clampf(sc.z, lo.z, hi.z));
    if (!inside) {
        const math::Vec3 d = sc - c;
        const f32 dist = d.length();
        if (dist >= r) return false;
        const math::Vec3 n_sphere_to_box = dist > kEps ? (c - sc) / dist : math::Vec3(0, -1, 0);
        out.normal = n_sphere_to_box;
        out.penetration = r - dist;
        out.point = c;
        return true;
    }
    // Centre inside the box: exit through the nearest face.
    const f32 dx_hi = hi.x - sc.x, dx_lo = sc.x - lo.x;
    const f32 dy_hi = hi.y - sc.y, dy_lo = sc.y - lo.y;
    const f32 dz_hi = hi.z - sc.z, dz_lo = sc.z - lo.z;
    f32 depth = dx_hi; math::Vec3 exit_dir(1, 0, 0);
    if (dx_lo < depth) { depth = dx_lo; exit_dir = math::Vec3(-1, 0, 0); }
    if (dy_hi < depth) { depth = dy_hi; exit_dir = math::Vec3(0, 1, 0); }
    if (dy_lo < depth) { depth = dy_lo; exit_dir = math::Vec3(0, -1, 0); }
    if (dz_hi < depth) { depth = dz_hi; exit_dir = math::Vec3(0, 0, 1); }
    if (dz_lo < depth) { depth = dz_lo; exit_dir = math::Vec3(0, 0, -1); }
    out.normal = exit_dir * -1.0f;               // sphere -> box
    out.penetration = r + depth;
    out.point = c;
    return true;
}

bool box_box(const math::Vec3& ac, const math::Vec3& ah, const math::Vec3& bc, const math::Vec3& bh,
             Contact& out) {
    const f32 ox = ah.x + bh.x - std::fabs(bc.x - ac.x);
    const f32 oy = ah.y + bh.y - std::fabs(bc.y - ac.y);
    const f32 oz = ah.z + bh.z - std::fabs(bc.z - ac.z);
    if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) return false;
    f32 pen = ox; math::Vec3 n(1, 0, 0);
    if (oy < pen) { pen = oy; n = math::Vec3(0, 1, 0); }
    if (oz < pen) { pen = oz; n = math::Vec3(0, 0, 1); }
    const f32 s = (bc - ac).dot(n) >= 0.0f ? 1.0f : -1.0f;
    out.normal = n * s;                          // a -> b
    out.penetration = pen;
    out.point = (ac + bc) * 0.5f;
    return true;
}

/// Capsules collapse to the endpoint sphere with the deepest contact. This is
/// the mobile-grade approximation; it is exact for upright capsules on flats.
bool capsule_as_spheres(const Body& cap, const Body& other, Contact& out) {
    const f32 hh = cap.collider.capsule_half_height;
    bool found = false;
    for (int end = -1; end <= 1; end += 2) {
        Body sphere = cap;
        sphere.collider.type = ShapeType::Sphere;
        sphere.position = cap.position + math::Vec3(0, static_cast<f32>(end) * hh, 0);
        Contact c;
        if (World3D::collide(sphere, other, c) && (!found || c.penetration > out.penetration)) {
            out = c;
            // Re-anchor the normal relative to the capsule centre, not the
            // endpoint: the endpoint shift is along Y only, so the normal is
            // unchanged; only the contact point matters.
            found = true;
        }
    }
    return found;
}

} // namespace

bool World3D::collide(const Body& a, const Body& b, Contact& out) {
    const bool a_cap = a.collider.type == ShapeType::Capsule;
    const bool b_cap = b.collider.type == ShapeType::Capsule;
    if (a_cap) return capsule_as_spheres(a, b, out);
    if (b_cap) {
        Contact c;
        const bool hit = capsule_as_spheres(b, a, c);
        if (hit) { c.normal = c.normal * -1.0f; out = c; }   // flip to a -> b
        return hit;
    }
    if (a.collider.type == ShapeType::Sphere && b.collider.type == ShapeType::Sphere)
        return sphere_sphere(a.position, a.collider.radius, b.position, b.collider.radius, out);
    if (a.collider.type == ShapeType::Sphere && b.collider.type == ShapeType::Box)
        return sphere_box(a.position, a.collider.radius, b.position, b.collider.half, out);
    if (a.collider.type == ShapeType::Box && b.collider.type == ShapeType::Sphere) {
        Contact c;
        if (!sphere_box(b.position, b.collider.radius, a.position, a.collider.half, c)) return false;
        c.normal = c.normal * -1.0f;                 // sphere->box becomes box->sphere
        out = c;
        return true;
    }
    return box_box(a.position, a.collider.half, b.position, b.collider.half, out);
}

// ------------------------------------------------------------------ world --
World3D::World3D() = default;
World3D::World3D(const Config& c) : cfg_(c) {}

u32 World3D::add(const BodyConfig& bc) {
    Body b;
    b.id = next_id_++;
    b.position = bc.position;
    b.velocity = bc.velocity;
    b.collider = bc.collider;
    b.is_static = bc.is_static;
    b.restitution = bc.restitution;
    b.friction = bc.friction;
    b.mass = bc.is_static ? 0.0f : (bc.mass > 0.0f ? bc.mass : 1.0f);
    b.inv_mass = b.is_static ? 0.0f : 1.0f / b.mass;
    bodies_.push_back(b);
    return b.id;
}

void World3D::remove(u32 id) {
    bodies_.erase(std::remove_if(bodies_.begin(), bodies_.end(),
                                 [id](const Body& b) { return b.id == id; }),
                  bodies_.end());
}

Body* World3D::get(u32 id) {
    for (auto& b : bodies_) if (b.id == id) return &b;
    return nullptr;
}
const Body* World3D::get(u32 id) const {
    for (const auto& b : bodies_) if (b.id == id) return &b;
    return nullptr;
}

std::size_t World3D::awake_count() const {
    std::size_t n = 0;
    for (const auto& b : bodies_) if (!b.sleeping && !b.is_static) ++n;
    return n;
}

void World3D::wake(u32 id) {
    Body* b = get(id);
    if (b) { b->sleeping = false; b->sleep_timer = 0; }
}

void World3D::integrate(f32 dt) {
    for (auto& b : bodies_) {
        if (b.is_static || b.sleeping) continue;
        b.velocity = b.velocity + cfg_.gravity * dt;
        b.position = b.position + b.velocity * dt;
    }
}

void World3D::solve() {
    contacts_.clear();
    // Broadphase + narrowphase.
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        for (std::size_t j = i + 1; j < bodies_.size(); ++j) {
            const Body& a = bodies_[i];
            const Body& b = bodies_[j];
            if (a.is_static && b.is_static) continue;
            if (a.sleeping && b.sleeping) continue;
            const math::Vec3 ha = a.collider.aabb_half();
            const math::Vec3 hb = b.collider.aabb_half();
            if (std::fabs(a.position.x - b.position.x) > ha.x + hb.x) continue;
            if (std::fabs(a.position.y - b.position.y) > ha.y + hb.y) continue;
            if (std::fabs(a.position.z - b.position.z) > ha.z + hb.z) continue;
            Contact c;
            if (collide(a, b, c)) {
                c.a = a.id;
                c.b = b.id;
                contacts_.push_back(c);
                // Only a real impact (not the resting contact a sleeping body
                // has with whatever it lies on) is allowed to wake bodies up.
                const f32 vn = (b.velocity - a.velocity).dot(c.normal);
                if (vn < -1.0f) {
                    if (a.sleeping) wake(a.id);
                    if (b.sleeping) wake(b.id);
                }
            }
        }
    }

    // Impulse solver.
    for (i32 it = 0; it < cfg_.velocity_iterations; ++it) {
        for (const auto& c : contacts_) {
            Body* a = get(c.a);
            Body* b = get(c.b);
            if (!a || !b) continue;
            const f32 inv_sum = a->inv_mass + b->inv_mass;
            if (inv_sum <= 0.0f) continue;
            const math::Vec3 rv = b->velocity - a->velocity;
            const f32 vn = rv.dot(c.normal);
            if (vn < 0.0f) {
                const f32 e = vn < -1.0f ? std::max(a->restitution, b->restitution) : 0.0f;
                const f32 j = -(1.0f + e) * vn / inv_sum;
                a->velocity = a->velocity - c.normal * (j * a->inv_mass);
                b->velocity = b->velocity + c.normal * (j * b->inv_mass);

                // Coulomb friction on the tangent plane.
                const math::Vec3 rv2 = b->velocity - a->velocity;
                const math::Vec3 t = rv2 - c.normal * rv2.dot(c.normal);
                const f32 tl = t.length();
                if (tl > kEps) {
                    const math::Vec3 tn = t / tl;
                    f32 jt = -rv2.dot(tn) / inv_sum;
                    const f32 mu = std::sqrt(a->friction * b->friction);
                    jt = clampf(jt, -mu * j, mu * j);
                    a->velocity = a->velocity - tn * (jt * a->inv_mass);
                    b->velocity = b->velocity + tn * (jt * b->inv_mass);
                }
            }
        }
    }

    // Positional correction (Baumgarte-lite, split impulses on positions).
    for (const auto& c : contacts_) {
        Body* a = get(c.a);
        Body* b = get(c.b);
        if (!a || !b) continue;
        const f32 inv_sum = a->inv_mass + b->inv_mass;
        if (inv_sum <= 0.0f) continue;
        const f32 corr = std::max(c.penetration - cfg_.positional_slop, 0.0f) / inv_sum * cfg_.positional_percent;
        a->position = a->position - c.normal * (corr * a->inv_mass);
        b->position = b->position + c.normal * (corr * b->inv_mass);
    }
}

void World3D::step(f32 dt) {
    integrate(dt);
    solve();
    // Sleep bookkeeping.
    for (auto& b : bodies_) {
        if (b.is_static) continue;
        if (b.velocity.length_sq() < cfg_.sleep_speed * cfg_.sleep_speed) {
            b.sleep_timer += dt;
            if (b.sleep_timer >= cfg_.sleep_time) {
                b.sleeping = true;
                b.velocity = math::Vec3();
            }
        } else {
            b.sleep_timer = 0;
            b.sleeping = false;
        }
    }
}

bool World3D::raycast(math::Vec3 from, math::Vec3 dir, f32 max_distance, RayHit& out) const {
    const f32 len = dir.length();
    if (len <= kEps || max_distance <= 0.0f) return false;
    const math::Vec3 d = dir / len;
    bool found = false;
    f32 best = std::numeric_limits<f32>::max();
    for (const auto& b : bodies_) {
        f32 t = -1.0f;
        math::Vec3 n;
        if (b.collider.type == ShapeType::Sphere || b.collider.type == ShapeType::Capsule) {
            const math::Vec3 oc = from - b.position;
            const f32 r = b.collider.type == ShapeType::Capsule
                              ? b.collider.radius + b.collider.capsule_half_height
                              : b.collider.radius;
            const f32 bq = oc.dot(d);
            const f32 cq = oc.length_sq() - r * r;
            const f32 disc = bq * bq - cq;
            if (disc >= 0.0f) {
                const f32 s = std::sqrt(disc);
                const f32 t0 = -bq - s;
                t = t0 >= 0.0f ? t0 : (-bq + s);
                if (t >= 0.0f) n = (from + d * t - b.position).normalized();
            }
        } else {
            // Slab method on the AABB.
            const math::Vec3 lo = b.position - b.collider.half;
            const math::Vec3 hi = b.position + b.collider.half;
            f32 tmin = 0.0f, tmax = std::numeric_limits<f32>::max();
            const f32 o[3] = {from.x, from.y, from.z};
            const f32 dd[3] = {d.x, d.y, d.z};
            const f32 l[3] = {lo.x, lo.y, lo.z};
            const f32 h[3] = {hi.x, hi.y, hi.z};
            f32 ax[3] = {0, 0, 0};
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                if (std::fabs(dd[i]) < kEps) {
                    if (o[i] < l[i] || o[i] > h[i]) { ok = false; break; }
                } else {
                    f32 t1 = (l[i] - o[i]) / dd[i];
                    f32 t2 = (h[i] - o[i]) / dd[i];
                    f32 axis_n = -1.0f;
                    if (t1 > t2) { const f32 tmp = t1; t1 = t2; t2 = tmp; axis_n = 1.0f; }
                    if (t1 > tmin) { tmin = t1; ax[0] = ax[1] = ax[2] = 0; ax[i] = axis_n; }
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) { ok = false; break; }
                }
            }
            if (ok && tmin >= 0.0f) { t = tmin; n = math::Vec3(ax[0], ax[1], ax[2]); }
        }
        if (t >= 0.0f && t <= max_distance && t < best) {
            best = t;
            out.body = b.id;
            out.point = from + d * t;
            out.normal = n;
            out.distance = t;
            found = true;
        }
    }
    return found;
}

} // namespace prism::physics3d
