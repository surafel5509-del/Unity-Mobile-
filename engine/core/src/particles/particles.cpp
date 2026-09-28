// =====================================================================
//  PRISM ENGINE — particles/particles.cpp
// =====================================================================
#include "prism/particles/particles.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace prism::particles {

namespace {
inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

// ================================================================ curves ==
f32 eval_curve(const std::vector<FloatStop>& stops, f32 t, f32 fallback) {
    if (stops.empty()) return fallback;
    if (t <= stops.front().t) return stops.front().v;
    if (t >= stops.back().t) return stops.back().v;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].t) {
            const f32 span = stops[i].t - stops[i - 1].t;
            const f32 f = span <= 0.0f ? 0.0f : (t - stops[i - 1].t) / span;
            return stops[i - 1].v + (stops[i].v - stops[i - 1].v) * f;
        }
    }
    return stops.back().v;
}

math::Color eval_gradient(const std::vector<ColorStop>& stops, f32 t, math::Color fallback) {
    if (stops.empty()) return fallback;
    if (t <= stops.front().t) return stops.front().c;
    if (t >= stops.back().t) return stops.back().c;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].t) {
            const f32 span = stops[i].t - stops[i - 1].t;
            const f32 f = span <= 0.0f ? 0.0f : (t - stops[i - 1].t) / span;
            const math::Color& a = stops[i - 1].c;
            const math::Color& b = stops[i].c;
            return math::Color{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f,
                               a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f};
        }
    }
    return stops.back().c;
}

// =============================================================== emitter ==
void EmitterShape::sample(Rng& rng, math::Vec3& out_pos, math::Vec3& out_dir) const {
    out_pos = math::Vec3();
    out_dir = math::Vec3(0, 1, 0);
    switch (kind) {
        case Kind::Point:
            break;
        case Kind::Sphere: {
            const f32 z = rng.range(-1.0f, 1.0f);
            const f32 theta = rng.range(0.0f, math::TwoPi);
            const f32 r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const math::Vec3 dir(r * std::cos(theta), z, r * std::sin(theta));
            out_dir = dir;
            out_pos = dir * (radius * std::cbrt(rng.unit()));
            break;
        }
        case Kind::Box:
            out_pos = math::Vec3(rng.range(-extents.x, extents.x), rng.range(-extents.y, extents.y),
                                 rng.range(-extents.z, extents.z));
            break;
        case Kind::Cone: {
            const f32 cos_h = std::cos(cone_half_angle);
            const f32 cos_a = cos_h + (1.0f - cos_h) * rng.unit();
            const f32 sin_a = std::sqrt(std::max(0.0f, 1.0f - cos_a * cos_a));
            const f32 theta = rng.range(0.0f, math::TwoPi);
            math::Vec3 up = cone_dir.normalized();
            math::Vec3 u = std::fabs(up.y) < 0.9f ? up.cross(math::Vec3(0, 1, 0)).normalized()
                                                 : up.cross(math::Vec3(1, 0, 0)).normalized();
            const math::Vec3 v = up.cross(u);
            out_dir = (up * cos_a + (u * std::cos(theta) + v * std::sin(theta)) * sin_a).normalized();
            break;
        }
    }
}

// ================================================================ system ==
ParticleSystem::ParticleSystem(const ParticleConfig& c, u32 seed) : cfg_(c) {
    rng_.seed(seed ? seed : 1u);
    parts_.resize(static_cast<std::size_t>(cfg_.capacity > 0 ? cfg_.capacity : 1));
}

void ParticleSystem::spawn_one() {
    if (alive_ >= cfg_.capacity) return;             // hard cap, like a real pool
    for (auto& p : parts_) {
        if (p.alive) continue;
        math::Vec3 dir;
        cfg_.shape.sample(rng_, p.position, dir);
        const f32 speed = cfg_.velocity.length();
        if (cfg_.shape.kind == EmitterShape::Kind::Point) {
            p.velocity = cfg_.velocity;
        } else {
            p.velocity = dir * speed;
        }
        if (cfg_.speed_randomness > 0.0f)
            p.velocity = p.velocity * (1.0f + rng_.range(-cfg_.speed_randomness, cfg_.speed_randomness));
        p.life = cfg_.life;
        if (cfg_.life_randomness > 0.0f)
            p.life *= 1.0f + rng_.range(-cfg_.life_randomness, cfg_.life_randomness);
        p.age = 0;
        p.size = cfg_.base_size;
        p.rotation = 0;
        p.seed = rng_.next();
        p.alive = true;
        ++alive_;
        ++spawned_;
        return;
    }
}

void ParticleSystem::burst(i32 count) {
    for (i32 i = 0; i < count; ++i) spawn_one();
}

void ParticleSystem::update(f32 dt) {
    if (dt <= 0.0f) return;
    time_ += dt;
    if (emitting_ && cfg_.rate > 0.0f) {
        emit_acc_ += cfg_.rate * dt;
        const i32 n = static_cast<i32>(emit_acc_);
        emit_acc_ -= static_cast<f32>(n);
        for (i32 i = 0; i < n; ++i) spawn_one();
    }
    for (auto& p : parts_) {
        if (!p.alive) continue;
        p.age += dt;
        if (p.age >= p.life) {
            p.alive = false;
            --alive_;
            continue;
        }
        p.velocity = p.velocity + cfg_.gravity * dt;
        if (cfg_.turbulence > 0.0f) {
            const f32 f = cfg_.turbulence_frequency;
            const math::Vec3 turb(std::sin(p.position.y * f + time_ * 1.7f),
                                  std::sin(p.position.z * f + time_ * 1.3f),
                                  std::sin(p.position.x * f + time_ * 2.1f));
            p.velocity = p.velocity + turb * (cfg_.turbulence * dt);
        }
        p.position = p.position + p.velocity * dt;
        p.rotation += cfg_.spin * dt;
        if (cfg_.collide_ground && p.position.y < cfg_.ground_y && p.velocity.y < 0.0f) {
            p.position.y = cfg_.ground_y;
            p.velocity.y = -p.velocity.y * cfg_.restitution;
            p.velocity.x *= 0.98f;
            p.velocity.z *= 0.98f;
        }
    }
}

void ParticleSystem::instances(std::vector<ParticleInstance>& out) const {
    out.clear();
    out.reserve(static_cast<std::size_t>(alive_));
    for (const auto& p : parts_) {
        if (!p.alive) continue;
        const f32 t01 = p.life > 0.0f ? p.age / p.life : 1.0f;
        ParticleInstance inst;
        inst.position = p.position;
        inst.size = p.size * eval_curve(cfg_.size_over_life, t01, 1.0f);
        inst.rotation = p.rotation;
        inst.color = eval_gradient(cfg_.color_over_life, t01, math::Color{1, 1, 1, 1});
        out.push_back(std::move(inst));
    }
}

bool ParticleSystem::bounds(math::Vec3& out_min, math::Vec3& out_max) const {
    if (alive_ == 0) return false;
    bool first = true;
    for (const auto& p : parts_) {
        if (!p.alive) continue;
        if (first) {
            out_min = out_max = p.position;
            first = false;
            continue;
        }
        out_min.x = std::min(out_min.x, p.position.x);
        out_min.y = std::min(out_min.y, p.position.y);
        out_min.z = std::min(out_min.z, p.position.z);
        out_max.x = std::max(out_max.x, p.position.x);
        out_max.y = std::max(out_max.y, p.position.y);
        out_max.z = std::max(out_max.z, p.position.z);
    }
    return !first;
}

u32 ParticleSystem::checksum() const {
    u32 acc = 0x811C9DC5u;
    for (const auto& p : parts_) {
        if (!p.alive) continue;
        u32 h = 0x811C9DC5u;
        auto mix = [&h](f32 v) {
            u32 bits;
            std::memcpy(&bits, &v, 4);
            h ^= bits;
            h *= 0x01000193u;
        };
        mix(p.position.x); mix(p.position.y); mix(p.position.z);
        mix(p.velocity.x); mix(p.velocity.y); mix(p.velocity.z);
        mix(p.age);
        acc += h;                                   // order-independent combine
    }
    return acc;
}

} // namespace prism::particles
