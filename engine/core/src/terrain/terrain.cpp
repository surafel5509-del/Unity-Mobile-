// =====================================================================
//  PRISM ENGINE — terrain/terrain.cpp
// =====================================================================
#include "prism/terrain/terrain.h"

#include <algorithm>
#include <cmath>

#include "prism/core/engine.h"   // Rng

namespace prism::terrain {

namespace {
inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline f32 falloff(f32 d, f32 radius) {
    if (d >= radius) return 0.0f;
    return 0.5f * (1.0f + std::cos(math::Pi * d / radius));
}
} // namespace

Heightmap::Heightmap(i32 resolution, f32 size, math::Vec3 origin)
    : n_(resolution >= 2 ? resolution : 2), size_(size > 0.0f ? size : 1.0f),
      cell_(size_ / static_cast<f32>(n_ - 1)), origin_(origin),
      h_(static_cast<std::size_t>(n_) * n_, 0.0f) {}

bool Heightmap::inside(f32 x, f32 z) const {
    return x >= origin_.x && x <= origin_.x + size_ && z >= origin_.z && z <= origin_.z + size_;
}

f32 Heightmap::vertex(i32 x, i32 z) const {
    return at(clampi(x, 0, n_ - 1), clampi(z, 0, n_ - 1));
}

f32 Heightmap::height_at(f32 x, f32 z) const {
    if (n_ < 2) return 0.0f;
    f32 gx = (x - origin_.x) / cell_;
    f32 gz = (z - origin_.z) / cell_;
    gx = clampf(gx, 0.0f, static_cast<f32>(n_ - 1));
    gz = clampf(gz, 0.0f, static_cast<f32>(n_ - 1));
    const i32 x0 = static_cast<i32>(std::floor(gx));
    const i32 z0 = static_cast<i32>(std::floor(gz));
    const i32 x1 = std::min(x0 + 1, n_ - 1);
    const i32 z1 = std::min(z0 + 1, n_ - 1);
    const f32 tx = gx - static_cast<f32>(x0);
    const f32 tz = gz - static_cast<f32>(z0);
    const f32 top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
    const f32 bot = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
    return top + (bot - top) * tz;
}

math::Vec3 Heightmap::normal_at(f32 x, f32 z) const {
    const f32 hl = height_at(x - cell_, z);
    const f32 hr = height_at(x + cell_, z);
    const f32 hd = height_at(x, z - cell_);
    const f32 hu = height_at(x, z + cell_);
    return math::Vec3(hl - hr, 2.0f * cell_, hd - hu).normalized();
}

void Heightmap::raise(f32 x, f32 z, f32 radius, f32 strength) {
    if (radius <= 0.0f) return;
    const i32 x0 = std::max(0, static_cast<i32>(std::floor((x - radius - origin_.x) / cell_)));
    const i32 x1 = std::min(n_ - 1, static_cast<i32>(std::ceil((x + radius - origin_.x) / cell_)));
    const i32 z0 = std::max(0, static_cast<i32>(std::floor((z - radius - origin_.z) / cell_)));
    const i32 z1 = std::min(n_ - 1, static_cast<i32>(std::ceil((z + radius - origin_.z) / cell_)));
    for (i32 zi = z0; zi <= z1; ++zi) {
        for (i32 xi = x0; xi <= x1; ++xi) {
            const f32 wx = origin_.x + static_cast<f32>(xi) * cell_;
            const f32 wz = origin_.z + static_cast<f32>(zi) * cell_;
            const f32 d = std::sqrt((wx - x) * (wx - x) + (wz - z) * (wz - z));
            at(xi, zi) += strength * falloff(d, radius);
        }
    }
}

void Heightmap::smooth(f32 x, f32 z, f32 radius, f32 amount) {
    if (radius <= 0.0f) return;
    std::vector<f32> copy = h_;
    const i32 x0 = std::max(0, static_cast<i32>(std::floor((x - radius - origin_.x) / cell_)));
    const i32 x1 = std::min(n_ - 1, static_cast<i32>(std::ceil((x + radius - origin_.x) / cell_)));
    const i32 z0 = std::max(0, static_cast<i32>(std::floor((z - radius - origin_.z) / cell_)));
    const i32 z1 = std::min(n_ - 1, static_cast<i32>(std::ceil((z + radius - origin_.z) / cell_)));
    for (i32 zi = z0; zi <= z1; ++zi) {
        for (i32 xi = x0; xi <= x1; ++xi) {
            const f32 wx = origin_.x + static_cast<f32>(xi) * cell_;
            const f32 wz = origin_.z + static_cast<f32>(zi) * cell_;
            const f32 w = falloff(std::sqrt((wx - x) * (wx - x) + (wz - z) * (wz - z)), radius);
            if (w <= 0.0f) continue;
            f32 sum = copy[static_cast<std::size_t>(zi) * n_ + xi];
            f32 cnt = 1.0f;
            if (xi > 0) { sum += copy[static_cast<std::size_t>(zi) * n_ + xi - 1]; cnt += 1.0f; }
            if (xi < n_ - 1) { sum += copy[static_cast<std::size_t>(zi) * n_ + xi + 1]; cnt += 1.0f; }
            if (zi > 0) { sum += copy[static_cast<std::size_t>(zi - 1) * n_ + xi]; cnt += 1.0f; }
            if (zi < n_ - 1) { sum += copy[static_cast<std::size_t>(zi + 1) * n_ + xi]; cnt += 1.0f; }
            const f32 avg = sum / cnt;
            const f32 cur = at(xi, zi);
            at(xi, zi) = cur + (avg - cur) * (amount * w);
        }
    }
}

void Heightmap::flatten(f32 x, f32 z, f32 radius, f32 target, f32 amount) {
    if (radius <= 0.0f) return;
    const i32 x0 = std::max(0, static_cast<i32>(std::floor((x - radius - origin_.x) / cell_)));
    const i32 x1 = std::min(n_ - 1, static_cast<i32>(std::ceil((x + radius - origin_.x) / cell_)));
    const i32 z0 = std::max(0, static_cast<i32>(std::floor((z - radius - origin_.z) / cell_)));
    const i32 z1 = std::min(n_ - 1, static_cast<i32>(std::ceil((z + radius - origin_.z) / cell_)));
    for (i32 zi = z0; zi <= z1; ++zi) {
        for (i32 xi = x0; xi <= x1; ++xi) {
            const f32 wx = origin_.x + static_cast<f32>(xi) * cell_;
            const f32 wz = origin_.z + static_cast<f32>(zi) * cell_;
            const f32 w = falloff(std::sqrt((wx - x) * (wx - x) + (wz - z) * (wz - z)), radius);
            if (w <= 0.0f) continue;
            const f32 cur = at(xi, zi);
            at(xi, zi) = cur + (target - cur) * (amount * w);
        }
    }
}

bool Heightmap::raycast(math::Vec3 from, math::Vec3 dir, f32 max_distance, math::Vec3& out_point) const {
    const f32 len = dir.length();
    if (len <= 1e-6f || max_distance <= 0.0f) return false;
    const math::Vec3 d = dir / len;
    const f32 step = cell_ * 0.25f;
    f32 prev_t = 0.0f;
    f32 prev_f = (from.y - height_at(from.x, from.z));
    for (f32 t = step; t <= max_distance; t += step) {
        const math::Vec3 p = from + d * t;
        const f32 f = p.y - height_at(p.x, p.z);
        if (f <= 0.0f && prev_f > 0.0f) {
            f32 lo = prev_t, hi = t;
            for (int i = 0; i < 16; ++i) {
                const f32 mid = (lo + hi) * 0.5f;
                const math::Vec3 mp = from + d * mid;
                if (mp.y - height_at(mp.x, mp.z) <= 0.0f) hi = mid;
                else lo = mid;
            }
            out_point = from + d * ((lo + hi) * 0.5f);
            return true;
        }
        prev_t = t;
        prev_f = f;
    }
    return false;
}

assets::MeshData Heightmap::build_mesh(i32 step, const std::string& name) const {
    assets::MeshData m;
    m.name = name;
    step = step < 1 ? 1 : step;
    const i32 per_side = (n_ - 1) / step + 1;
    for (i32 zi = 0; zi < per_side; ++zi) {
        for (i32 xi = 0; xi < per_side; ++xi) {
            const i32 gx = std::min(xi * step, n_ - 1);
            const i32 gz = std::min(zi * step, n_ - 1);
            const f32 wx = origin_.x + static_cast<f32>(gx) * cell_;
            const f32 wz = origin_.z + static_cast<f32>(gz) * cell_;
            assets::MeshVertex v;
            v.position = math::Vec3(wx, at(gx, gz), wz);
            v.normal = normal_at(wx, wz);
            v.uv = math::Vec2(static_cast<f32>(gx) / static_cast<f32>(n_ - 1),
                              static_cast<f32>(gz) / static_cast<f32>(n_ - 1));
            m.vertices.push_back(v);
        }
    }
    for (i32 zi = 0; zi < per_side - 1; ++zi) {
        for (i32 xi = 0; xi < per_side - 1; ++xi) {
            const u32 a = static_cast<u32>(zi * per_side + xi);
            const u32 b = a + static_cast<u32>(per_side);
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(a + 1);
            m.indices.push_back(a + 1);
            m.indices.push_back(b);
            m.indices.push_back(b + 1);
        }
    }
    m.compute_bounds();
    return m;
}

f32 Heightmap::min_height() const {
    f32 m = h_.empty() ? 0.0f : h_[0];
    for (f32 v : h_) m = std::min(m, v);
    return m;
}
f32 Heightmap::max_height() const {
    f32 m = h_.empty() ? 0.0f : h_[0];
    for (f32 v : h_) m = std::max(m, v);
    return m;
}

// ---------------------------------------------------------------- foliage ==
FoliagePainter::FoliagePainter(const Heightmap& hm)
    : hm_(&hm), density_(static_cast<std::size_t>(hm.resolution()) * hm.resolution(), 0.0f) {}

void FoliagePainter::paint(f32 x, f32 z, f32 radius, f32 amount) {
    const i32 n = hm_->resolution();
    for (i32 zi = 0; zi < n; ++zi) {
        for (i32 xi = 0; xi < n; ++xi) {
            const f32 wx = hm_->origin().x + static_cast<f32>(xi) * hm_->cell();
            const f32 wz = hm_->origin().z + static_cast<f32>(zi) * hm_->cell();
            const f32 w = falloff(std::sqrt((wx - x) * (wx - x) + (wz - z) * (wz - z)), radius);
            if (w <= 0.0f) continue;
            f32& d = density_[static_cast<std::size_t>(zi) * n + xi];
            d = clampf(d + amount * w, 0.0f, 1.0f);
        }
    }
}

void FoliagePainter::erase(f32 x, f32 z, f32 radius, f32 amount) { paint(x, z, radius, -amount); }

void FoliagePainter::clear() { std::fill(density_.begin(), density_.end(), 0.0f); }

f32 FoliagePainter::density_at(f32 x, f32 z) const {
    const i32 n = hm_->resolution();
    const i32 xi = clampi(static_cast<i32>(std::round((x - hm_->origin().x) / hm_->cell())), 0, n - 1);
    const i32 zi = clampi(static_cast<i32>(std::round((z - hm_->origin().z) / hm_->cell())), 0, n - 1);
    return density_[static_cast<std::size_t>(zi) * n + xi];
}

std::vector<FoliageInstance> FoliagePainter::scatter(u32 seed, u32 type_count) const {
    std::vector<FoliageInstance> out;
    const i32 n = hm_->resolution();
    Rng rng(seed ? seed : 1u);
    for (i32 zi = 0; zi < n; ++zi) {
        for (i32 xi = 0; xi < n; ++xi) {
            const f32 d = density_[static_cast<std::size_t>(zi) * n + xi];
            if (d <= 0.0f) continue;
            if (rng.unit() >= d) continue;
            FoliageInstance inst;
            const f32 jx = rng.range(-0.4f, 0.4f) * hm_->cell();
            const f32 jz = rng.range(-0.4f, 0.4f) * hm_->cell();
            const f32 wx = hm_->origin().x + static_cast<f32>(xi) * hm_->cell() + jx;
            const f32 wz = hm_->origin().z + static_cast<f32>(zi) * hm_->cell() + jz;
            inst.position = math::Vec3(wx, hm_->height_at(wx, wz), wz);
            inst.yaw = rng.range(0.0f, math::TwoPi);
            inst.scale = rng.range(0.8f, 1.2f);
            inst.type = type_count > 0 ? rng.next() % type_count : 0;
            out.push_back(std::move(inst));
        }
    }
    return out;
}

f32 FoliagePainter::painted_area() const {
    f32 sum = 0;
    for (f32 d : density_) sum += d;
    return sum * hm_->cell() * hm_->cell();
}

} // namespace prism::terrain
