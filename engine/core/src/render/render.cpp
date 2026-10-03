// PRISM ENGINE — render/render.cpp
#include "prism/render/render.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace prism::render {

using math::Vec2;
using math::Vec3;
using math::Vec4;
using math::Mat4;

const char* backend_name(Backend b) {
    switch (b) {
        case Backend::Vulkan13: return "Vulkan 1.3";
        case Backend::Gles32:   return "OpenGL ES 3.2";
        case Backend::Null:     return "Null";
    }
    return "Null";
}

const char* render_path_name(RenderPath p) {
    switch (p) {
        case RenderPath::MobileOptimized: return "Mobile-Optimized";
        case RenderPath::Forward:         return "Forward";
        case RenderPath::ForwardPlus:     return "Forward+";
        case RenderPath::Deferred:        return "Deferred";
    }
    return "Forward";
}

RenderPath choose_render_path(const GpuProfile& gpu) {
    if (gpu.family == GpuFamily::Swiftshader || gpu.tier <= GpuTier::Low)
        return RenderPath::MobileOptimized;
    if (gpu.supports_deferred) return RenderPath::Deferred;
    if (gpu.supports_forward_plus) return RenderPath::ForwardPlus;
    return RenderPath::Forward;
}

// ============================================================ spectral ======
namespace spectral {
namespace {
constexpr f32 gamma_k = 0.8f;

f32 falloff(f32 nm) {
    if (nm >= 420.0f && nm <= 700.0f) return 1.0f;
    if (nm >= 380.0f && nm < 420.0f) return 0.3f + 0.7f * (nm - 380.0f) / 40.0f;
    if (nm > 700.0f && nm <= 780.0f) return 0.3f + 0.7f * (780.0f - nm) / 80.0f;
    return 0.0f;
}
} // namespace

math::Vec3 wavelength_to_linear_rgb(f32 nm) {
    f32 r = 0, g = 0, b = 0;
    if (nm >= 380.0f && nm < 440.0f) {
        r = -(nm - 440.0f) / (440.0f - 380.0f); g = 0.0f; b = 1.0f;
    } else if (nm >= 440.0f && nm < 490.0f) {
        r = 0.0f; g = (nm - 440.0f) / (490.0f - 440.0f); b = 1.0f;
    } else if (nm >= 490.0f && nm < 510.0f) {
        r = 0.0f; g = 1.0f; b = -(nm - 510.0f) / (510.0f - 490.0f);
    } else if (nm >= 510.0f && nm < 580.0f) {
        r = (nm - 510.0f) / (580.0f - 510.0f); g = 1.0f; b = 0.0f;
    } else if (nm >= 580.0f && nm < 645.0f) {
        r = 1.0f; g = -(nm - 645.0f) / (645.0f - 580.0f); b = 0.0f;
    } else if (nm >= 645.0f && nm <= 780.0f) {
        r = 1.0f; g = 0.0f; b = 0.0f;
    } else {
        return Vec3(0, 0, 0);
    }
    const f32 f = falloff(nm);
    auto adj = [f](f32 c) {
        return c <= 0.0f ? 0.0f : std::pow(c * f, gamma_k);
    };
    // Clip (do not scale) so spectral hue stays accurate at the ends.
    return Vec3(math::clampf(adj(r), 0.0f, 1.0f),
                math::clampf(adj(g), 0.0f, 1.0f),
                math::clampf(adj(b), 0.0f, 1.0f));
}

math::Vec3 wavelength_to_srgb(f32 nm) { return linear_to_srgb(wavelength_to_linear_rgb(nm)); }

f32 prism_deviation(f32 nm, f32 apex_rad, f32 incidence_rad, const Glass& g) {
    const f32 n = g.ior(nm);
    if (n <= 1.0f || apex_rad <= 0.0f) return std::numeric_limits<f32>::quiet_NaN();
    const f32 sin_i = std::sin(incidence_rad);
    const f32 sin_r1 = sin_i / n;
    if (sin_r1 > 1.0f || sin_r1 < -1.0f) return std::numeric_limits<f32>::quiet_NaN();
    const f32 r1 = std::asin(sin_r1);
    const f32 r2 = apex_rad - r1;
    const f32 sin_e = n * std::sin(r2);
    if (sin_e > 1.0f || sin_e < -1.0f) return std::numeric_limits<f32>::quiet_NaN();  // TIR
    const f32 e = std::asin(sin_e);
    return incidence_rad + e - apex_rad;
}

f32 angular_dispersion(f32 nm_lo, f32 nm_hi, f32 apex_rad, f32 incidence_rad, const Glass& g) {
    const f32 d_lo = prism_deviation(nm_lo, apex_rad, incidence_rad, g);
    const f32 d_hi = prism_deviation(nm_hi, apex_rad, incidence_rad, g);
    if (std::isnan(d_lo) || std::isnan(d_hi)) return std::numeric_limits<f32>::quiet_NaN();
    return std::fabs(d_hi - d_lo);
}

f32 apex_for_spread(f32 nm_lo, f32 nm_hi, f32 target_rad, const Glass& g) {
    // Bisection on the apex angle; dispersion grows monotonically with it
    // until total internal reflection cuts the beam off.
    f32 lo = 1.0f * math::Deg2Rad, hi = 80.0f * math::Deg2Rad;
    const f32 incidence = 0.0f;   // search at normal incidence for stability
    for (int i = 0; i < 48; ++i) {
        const f32 mid = (lo + hi) * 0.5f;
        const f32 spread = angular_dispersion(nm_lo, nm_hi, mid, incidence, g);
        if (std::isnan(spread) || spread > target_rad) hi = mid;
        else lo = mid;
    }
    return (lo + hi) * 0.5f;
}

math::Vec3 thin_film(f32 thickness_nm, f32 cos_theta, f32 ior) {
    // Optical path difference inside the film, near-normal approximation with
    // the refraction angle folded in via cos_theta.
    const f32 cos_t = math::clampf(cos_theta, 0.05f, 1.0f);
    const f32 opd = 2.0f * ior * thickness_nm / cos_t;
    auto band = [&](f32 nm) {
        // Half-wave phase shift on the top reflection adds pi (the -1 term).
        const f32 phase = math::TwoPi * opd / nm + math::Pi;
        const f32 amp = std::sin(phase * 0.5f);
        return amp * amp;
    };
    return Vec3(math::clampf(band(650.0f), 0.0f, 1.0f),
                math::clampf(band(532.0f), 0.0f, 1.0f),
                math::clampf(band(460.0f), 0.0f, 1.0f));
}

std::vector<math::Vec3> spectrum_lut(i32 bands) {
    std::vector<math::Vec3> out;
    if (bands <= 0) return out;
    out.reserve(static_cast<std::size_t>(bands));
    for (i32 i = 0; i < bands; ++i) {
        const f32 t = bands == 1 ? 0.5f : static_cast<f32>(i) / static_cast<f32>(bands - 1);
        out.push_back(wavelength_to_linear_rgb(math::lerpf(kMinWavelengthNm, kMaxWavelengthNm, t)));
    }
    return out;
}

math::Vec2 dispersion_offset(f32 nm, f32 strength, math::Vec2 direction) {
    // Violet (380) bends most, red (700) least: normalise the deviation by the
    // refractive index of BK7 so the scale matches real glass.
    const Glass g;
    const f32 n = g.ior(nm);
    const f32 n_red = g.ior(700.0f);
    const f32 n_vio = g.ior(380.0f);
    const f32 t = (n - n_red) / std::fabs(n_vio - n_red + kEpsilon);   // 0 at red, 1 at violet
    const f32 s = strength * (t - 0.5f) * 2.0f;                        // centred on green
    return direction * s;
}
} // namespace spectral

// ============================================================== tonemap =====
const char* tonemap_name(TonemapOp op) {
    switch (op) {
        case TonemapOp::None:       return "none";
        case TonemapOp::Reinhard:   return "reinhard";
        case TonemapOp::Uncharted2: return "uncharted2";
        case TonemapOp::AcesFilmic: return "aces";
        case TonemapOp::Agx:        return "agx";
    }
    return "none";
}

f32 Exposure::compute_ev100() const {
    const f32 avg = std::max(average_luminance, 1e-4f);
    return std::log2((avg * 100.0f) / std::max(key_value, 1e-4f));
}

f32 Exposure::multiplier() const {
    f32 m = std::pow(2.0f, ev100);
    if (!std::isfinite(m) || m <= 0.0f) m = 1.0f;
    return math::clampf(m, min_exposure, max_exposure);
}

math::Vec3 linear_to_srgb(math::Vec3 c) {
    auto f = [](f32 v) {
        v = std::max(0.0f, v);
        return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    };
    return Vec3(f(c.x), f(c.y), f(c.z));
}

math::Vec3 srgb_to_linear(math::Vec3 c) {
    auto f = [](f32 v) {
        v = std::max(0.0f, v);
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return Vec3(f(c.x), f(c.y), f(c.z));
}

f32 luminance(math::Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

math::Vec3 tonemap(math::Vec3 hdr, TonemapOp op, f32 exposure) {
    auto clamp01 = [](f32 v) { return math::clampf(v, 0.0f, 1.0f); };
    const Vec3 x = hdr * std::max(exposure, 0.0f);
    switch (op) {
        case TonemapOp::None:
            return Vec3(clamp01(x.x), clamp01(x.y), clamp01(x.z));
        case TonemapOp::Reinhard: {
            auto f = [](f32 v) { return v / (1.0f + v); };
            return Vec3(f(x.x), f(x.y), f(x.z));
        }
        case TonemapOp::Uncharted2: {
            const f32 A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
            auto f = [&](f32 v) {
                return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F;
            };
            const f32 white = 11.2f;
            const f32 denom = f(white);
            return Vec3(clamp01(f(x.x) / denom), clamp01(f(x.y) / denom), clamp01(f(x.z) / denom));
        }
        case TonemapOp::AcesFilmic: {
            // Narkowicz fit — cheap enough for per-pixel mobile use.
            const f32 a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
            auto f = [&](f32 v) { return clamp01((v * (a * v + b)) / (v * (c * v + d) + e)); };
            return Vec3(f(x.x), f(x.y), f(x.z));
        }
        case TonemapOp::Agx: {
            // Simplified AgX: sigmoid shoulder with a mild desaturation of
            // highlights, which keeps saturated spectral colours from clipping
            // to flat white (important for the dispersion effect).
            auto f = [](f32 v) {
                const f32 t = 1.0f / (1.0f + std::exp(-(v - 0.5f) * 4.0f));
                return math::clampf(t * 1.05f - 0.02f, 0.0f, 1.0f);
            };
            Vec3 y(f(x.x), f(x.y), f(x.z));
            const f32 lum = luminance(Vec3(x.x, x.y, x.z));
            const f32 l2 = f(lum);
            const f32 blend = math::clampf(lum / 4.0f, 0.0f, 1.0f) * 0.3f;
            return Vec3(math::lerpf(y.x, l2, blend), math::lerpf(y.y, l2, blend),
                        math::lerpf(y.z, l2, blend));
        }
    }
    return Vec3(clamp01(x.x), clamp01(x.y), clamp01(x.z));
}

// =========================================================== frame graph ===
i64 GraphResource::estimated_bytes() const {
    const f32 bpp = GpuProfile::bits_per_pixel(format);
    if (bpp <= 0.0f) return 0;
    i64 total = 0;
    i64 level_bytes = static_cast<i64>(static_cast<f64>(width) * height * (bpp / 8.0));
    for (i32 m = 0; m < std::max(1, mip_levels); ++m) {
        total += level_bytes * std::max(1, array_layers);
        level_bytes = std::max<i64>(1, level_bytes / 4);
    }
    // Depth/stencil surfaces are counted as 32 bpp when format is unspecified.
    if (kind == Kind::Depth && format == TextureFormat::None)
        total = static_cast<i64>(width) * height * 4;
    return total;
}

void FrameGraph::declare(GraphResource r) { resources_.push_back(std::move(r)); }
void FrameGraph::add(GraphPass p) { declared_.push_back(std::move(p)); }
void FrameGraph::clear() { resources_.clear(); declared_.clear(); order_.clear(); culled_.clear(); cycle_ = false; }

const GraphResource* FrameGraph::resource(const std::string& name) const {
    for (const auto& r : resources_) if (r.name == name) return &r;
    return nullptr;
}

std::size_t FrameGraph::compile(const std::string& present_target, i32 quality_tier) {
    order_.clear();
    culled_.clear();
    cycle_ = false;

    // Work on a mutable copy so callers can re-compile at a different tier.
    std::vector<GraphPass> live = declared_;
    for (auto& p : live) p.culled = (p.optional && p.min_quality_tier > quality_tier);

    // Fixed point: a pass survives only if something downstream reads one of
    // its outputs, or it writes the present target.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < live.size(); ++i) {
            GraphPass& p = live[i];
            if (p.culled) continue;
            bool needed = false;
            for (const auto& w : p.writes) if (w == present_target) { needed = true; break; }
            if (!needed) {
                for (const auto& w : p.writes) {
                    for (std::size_t j = 0; j < live.size() && !needed; ++j) {
                        if (j == i || live[j].culled) continue;
                        for (const auto& r : live[j].reads) if (r == w) { needed = true; break; }
                    }
                    if (needed) break;
                }
            }
            if (!needed) { p.culled = true; culled_.push_back(p.name); changed = true; }
        }
    }

    // Kahn topological sort over the surviving passes.
    std::vector<GraphPass*> remaining;
    for (auto& p : live) if (!p.culled) remaining.push_back(&p);

    auto produces = [](GraphPass* p, const std::string& name) {
        for (const auto& w : p->writes) if (w == name) return true;
        return false;
    };

    std::vector<GraphPass*> sorted;
    std::vector<bool> done(remaining.size(), false);
    std::size_t emitted = 0;
    while (emitted < remaining.size()) {
        bool progress = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            if (done[i]) continue;
            bool ready = true;
            for (const auto& dep : remaining[i]->reads) {
                for (std::size_t j = 0; j < remaining.size() && ready; ++j) {
                    if (j == i || done[j]) continue;
                    if (produces(remaining[j], dep)) ready = false;
                }
                if (!ready) break;
            }
            if (ready) { done[i] = true; sorted.push_back(remaining[i]); ++emitted; progress = true; }
        }
        if (!progress) { cycle_ = true; break; }
    }

    order_.reserve(sorted.size());
    for (GraphPass* p : sorted) order_.push_back(*p);
    return order_.size();
}

i64 FrameGraph::peak_transient_bytes() const {
    i64 peak = 0;
    for (const auto& p : order_) {
        i64 live = 0;
        for (const auto& w : p.writes) {
            const GraphResource* r = resource(w);
            if (r && r->transient) live += r->estimated_bytes();
        }
        for (const auto& rr : p.reads) {
            const GraphResource* r = resource(rr);
            if (r && r->transient) live += r->estimated_bytes();
        }
        peak = std::max(peak, live);
    }
    return peak;
}

f32 FrameGraph::total_cost() const {
    f32 sum = 0;
    for (const auto& p : order_) sum += p.cost;
    return sum;
}

// ============================================================== culling =====
void Frustum::extract_from_view_proj(const math::Mat4& vp) {
    auto row = [&](int i) {
        return Vec4(vp.at(i, 0), vp.at(i, 1), vp.at(i, 2), vp.at(i, 3));
    };
    const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto add = [](Vec4 a, Vec4 b) { return Vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); };
    auto sub = [](Vec4 a, Vec4 b) { return Vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); };
    planes[0] = add(r3, r0);   // left
    planes[1] = sub(r3, r0);   // right
    planes[2] = add(r3, r1);   // bottom
    planes[3] = sub(r3, r1);   // top
    planes[4] = add(r3, r2);   // near  ([-1,1] clip range)
    planes[5] = sub(r3, r2);   // far
    for (auto& p : planes) {
        const f32 len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len > kEpsilon) { p.x /= len; p.y /= len; p.z /= len; p.w /= len; }
    }
}

bool Frustum::contains_sphere(math::Vec3 center, f32 radius) const {
    for (const auto& p : planes) {
        const f32 d = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (d < -radius) return false;
    }
    return true;
}

bool Frustum::contains_aabb(math::Vec3 mn, math::Vec3 mx) const {
    for (const auto& p : planes) {
        // positive vertex: the corner furthest along the plane normal
        const f32 px = p.x >= 0 ? mx.x : mn.x;
        const f32 py = p.y >= 0 ? mx.y : mn.y;
        const f32 pz = p.z >= 0 ? mx.z : mn.z;
        if (p.x * px + p.y * py + p.z * pz + p.w < 0) return false;
    }
    return true;
}

i32 LodSelector::select(f32 screen_coverage) const {
    const f32 c = screen_coverage * screen_coverage_bias;
    for (std::size_t i = 0; i < thresholds.size(); ++i)
        if (c > thresholds[i]) return static_cast<i32>(i);
    return static_cast<i32>(thresholds.size());
}

f32 screen_coverage(f32 bounding_radius, f32 distance, f32 fov_y_rad, i32 viewport_height) {
    if (distance <= kEpsilon || viewport_height <= 0) return 1.0f;
    const f32 proj = bounding_radius / (std::tan(fov_y_rad * 0.5f) * distance);
    return math::clampf(proj, 0.0f, 1.0f);
}

// ========================================================= sprite batch =====
void SpriteBatcher::begin() {
    quads_.clear();
    vertices_.clear();
    indices_.clear();
    batch_boundaries_.clear();
    stats_ = SpriteBatchStats{};
}

void SpriteBatcher::draw_quad(const math::Rect& rect, const math::Rect& uv,
                              const math::Color& tint, u64 texture, u32 shader, i32 order) {
    if (quads_.size() >= kMaxSprites) return;
    quads_.push_back(Quad{rect, uv, tint, texture, shader, order});
}

void SpriteBatcher::draw_nine_slice(const math::Rect& rect, const math::Rect& src,
                                    f32 bl, f32 br, f32 bt, f32 bb, const math::Color& tint,
                                    u64 texture, u32 shader, i32 order) {
    const f32 mid_w = std::max(0.0f, rect.w - bl - br);
    const f32 mid_h = std::max(0.0f, rect.h - bt - bb);
    const f32 su = src.w > kEpsilon ? 1.0f / src.w : 1.0f;
    const f32 sv = src.h > kEpsilon ? 1.0f / src.h : 1.0f;

    const f32 xs[3] = { rect.x, rect.x + bl, rect.x + bl + mid_w };
    const f32 ys[3] = { rect.y, rect.y + bt, rect.y + bt + mid_h };
    const f32 ws[3] = { bl, mid_w, br };
    const f32 hs[3] = { bt, mid_h, bb };
    const f32 us[4] = { src.x, src.x + bl * su, src.x + (bl + std::max(0.0f, src.w - bl - br)) * su,
                        src.x + src.w };
    const f32 vs[4] = { src.y, src.y + bt * sv, src.y + (bt + std::max(0.0f, src.h - bt - bb)) * sv,
                        src.y + src.h };

    for (int gy = 0; gy < 3; ++gy) {
        if (hs[gy] <= kEpsilon) continue;
        for (int gx = 0; gx < 3; ++gx) {
            if (ws[gx] <= kEpsilon) continue;
            math::Rect r{xs[gx], ys[gy], ws[gx], hs[gy]};
            math::Rect u{us[gx], vs[gy], us[gx + 1] - us[gx], vs[gy + 1] - vs[gy]};
            draw_quad(r, u, tint, texture, shader, order);
        }
    }
}

void SpriteBatcher::emit(const Quad& q) {
    const u32 base = static_cast<u32>(vertices_.size());
    auto push = [&](f32 x, f32 y, f32 u, f32 v) {
        vertices_.push_back(SpriteVertex{Vec2(x, y), Vec2(u, v), q.tint});
    };
    push(q.rect.x, q.rect.y, q.uv.x, q.uv.y);
    push(q.rect.x + q.rect.w, q.rect.y, q.uv.x + q.uv.w, q.uv.y);
    push(q.rect.x + q.rect.w, q.rect.y + q.rect.h, q.uv.x + q.uv.w, q.uv.y + q.uv.h);
    push(q.rect.x, q.rect.y + q.rect.h, q.uv.x, q.uv.y + q.uv.h);
    indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    stats_.vertices += 4;
    stats_.indices += 6;
}

std::size_t SpriteBatcher::end() {
    // Order-first sort: 2D correctness demands painter order, so shader and
    // texture keys only break ties within one order value.
    std::stable_sort(quads_.begin(), quads_.end(), [](const Quad& a, const Quad& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.shader != b.shader) return a.shader < b.shader;
        return a.tex < b.tex;
    });

    vertices_.clear();
    indices_.clear();
    batch_boundaries_.clear();
    stats_ = SpriteBatchStats{};
    stats_.sprites = static_cast<u32>(quads_.size());

    bool first = true;
    u64 prev_tex = 0; u32 prev_shader = 0; i32 prev_order = 0;
    for (const auto& q : quads_) {
        if (!first) {
            if (q.order != prev_order)            ++stats_.breaks_order;
            else if (q.shader != prev_shader)     ++stats_.breaks_shader;
            else if (q.tex != prev_tex)           ++stats_.breaks_texture;
        }
        if (first || q.tex != prev_tex || q.shader != prev_shader || q.order != prev_order) {
            batch_boundaries_.push_back(vertices_.size() / 4);
            ++stats_.batches;
        }
        emit(q);
        prev_tex = q.tex; prev_shader = q.shader; prev_order = q.order;
        first = false;
    }
    return stats_.batches;
}

// ====================================================== Forward+ lights =====
const std::vector<u32>& LightGrid::empty_list() {
    static const std::vector<u32> e;
    return e;
}

void LightGrid::resize(i32 tiles_x, i32 tiles_y, i32 tile_pixels) {
    tiles_x_ = std::max(1, tiles_x);
    tiles_y_ = std::max(1, tiles_y);
    tile_pixels_ = std::max(8, tile_pixels);
    tiles_.assign(static_cast<std::size_t>(tiles_x_) * tiles_y_, {});
    assignments_ = 0;
}

void LightGrid::rebuild(const std::vector<LightData>& lights, const math::Mat4& view_proj,
                        i32 viewport_w, i32 viewport_h) {
    for (auto& t : tiles_) t.clear();
    assignments_ = 0;
    if (tiles_.empty() || viewport_w <= 0 || viewport_h <= 0) return;

    for (std::size_t li = 0; li < lights.size(); ++li) {
        const LightData& L = lights[li];
        // Project the light centre, then expand by its radius in clip space.
        math::Vec4 clip = view_proj * Vec4(L.position, 1.0f);
        if (std::fabs(clip.w) < kEpsilon) continue;
        const f32 ndc_x = clip.x / clip.w;
        const f32 ndc_y = clip.y / clip.w;
        const f32 ndc_z = clip.z / clip.w;
        if (ndc_z < -1.0f || ndc_z > 1.0f) continue;             // outside depth range
        const f32 sx = (ndc_x * 0.5f + 0.5f) * viewport_w;
        const f32 sy = (1.0f - (ndc_y * 0.5f + 0.5f)) * viewport_h;
        // Conservative screen radius: scale the world radius by the perspective
        // divide at the light's distance from the camera plane.
        const f32 dist = std::max(std::fabs(clip.w), kEpsilon);
        const f32 radius_px = (L.radius / dist) * (viewport_h * 0.5f) * 2.0f;

        const i32 tx0 = std::max(0, static_cast<i32>(std::floor((sx - radius_px) / tile_pixels_)));
        const i32 tx1 = std::min(tiles_x_ - 1, static_cast<i32>(std::floor((sx + radius_px) / tile_pixels_)));
        const i32 ty0 = std::max(0, static_cast<i32>(std::floor((sy - radius_px) / tile_pixels_)));
        const i32 ty1 = std::min(tiles_y_ - 1, static_cast<i32>(std::floor((sy + radius_px) / tile_pixels_)));

        for (i32 ty = ty0; ty <= ty1; ++ty) {
            for (i32 tx = tx0; tx <= tx1; ++tx) {
                auto& list = tiles_[static_cast<std::size_t>(ty) * tiles_x_ + tx];
                if (static_cast<i32>(list.size()) >= max_per_tile_) continue;
                list.push_back(static_cast<u32>(li));
                ++assignments_;
            }
        }
    }
}

const std::vector<u32>& LightGrid::tile(i32 tx, i32 ty) const {
    if (tx < 0 || ty < 0 || tx >= tiles_x_ || ty >= tiles_y_) return empty_list();
    return tiles_[static_cast<std::size_t>(ty) * tiles_x_ + tx];
}

std::vector<u32> LightGrid::to_ssbo() const {
    // Layout: per tile [count][index...][padding to max_per_tile_]
    std::vector<u32> out;
    out.reserve(tiles_.size() * (static_cast<std::size_t>(max_per_tile_) + 1));
    for (const auto& list : tiles_) {
        out.push_back(static_cast<u32>(list.size()));
        for (u32 idx : list) out.push_back(idx);
        for (std::size_t i = list.size(); i < static_cast<std::size_t>(max_per_tile_); ++i)
            out.push_back(0xFFFFFFFFu);
    }
    return out;
}

// ====================================================== adaptive quality ====
f32 AdaptiveResolution::update(f32 dt, f32 fps) {
    stats_.fps = fps;
    stats_.target_fps = cfg_.target_fps;
    cooldown_ = std::max(0.0f, cooldown_ - dt);

    const f32 target = cfg_.target_fps;
    // Thermal headroom shrinks the ceiling: each Android thermal level above
    // NONE removes a step of scale so we throttle before the OS does it for us.
    const f32 thermal_drop = cfg_.thermal_step * static_cast<f32>(std::max(0, stats_.thermal_status));
    const f32 battery_drop = stats_.battery_saver ? cfg_.thermal_step : 0.0f;
    const f32 ceiling = math::clampf(cfg_.max_scale - thermal_drop - battery_drop,
                                     cfg_.min_scale, cfg_.max_scale);
    scale_ = std::min(scale_, ceiling);

    if (cooldown_ > 0.0f || fps <= 0.0f) { stats_.render_scale = scale_; return scale_; }

    const f32 low  = target * (1.0f - cfg_.tolerance);
    const f32 high = target * (1.0f + cfg_.tolerance);
    if (fps < low && scale_ > cfg_.min_scale) {
        scale_ = std::max(cfg_.min_scale, scale_ - cfg_.step);
        cooldown_ = cfg_.cooldown_seconds;
        ++stats_.adjustments;
    } else if (fps > high && scale_ < ceiling) {
        scale_ = std::min(ceiling, scale_ + cfg_.step);
        cooldown_ = cfg_.cooldown_seconds;
        ++stats_.adjustments;
    }
    stats_.render_scale = scale_;
    return scale_;
}

void AdaptiveResolution::set_thermal(i32 thermal_status, bool battery_saver) {
    stats_.thermal_status = std::max(0, thermal_status);
    stats_.battery_saver = battery_saver;
}

// ========================================================== post process ====
const char* post_effect_name(PostEffect e) {
    switch (e) {
        case PostEffect::None:                return "none";
        case PostEffect::Bloom:               return "bloom";
        case PostEffect::DepthOfField:        return "depth_of_field";
        case PostEffect::MotionBlur:          return "motion_blur";
        case PostEffect::ColorGrading:        return "color_grading";
        case PostEffect::AmbientOcclusion:    return "ambient_occlusion";
        case PostEffect::ScreenSpaceReflections: return "ssr";
        case PostEffect::ChromaticAberration: return "chromatic_aberration";
        case PostEffect::FilmGrain:           return "film_grain";
        case PostEffect::Vignette:            return "vignette";
        case PostEffect::SpectralDispersion:  return "spectral_dispersion";
        case PostEffect::Fx:                  return "fxaa";
    }
    return "none";
}

f32 post_effect_cost(PostEffect e) {
    switch (e) {
        case PostEffect::None:                return 0.0f;
        case PostEffect::Bloom:               return 1.6f;   // downsample chain + upsample
        case PostEffect::DepthOfField:        return 2.2f;
        case PostEffect::MotionBlur:          return 1.4f;
        case PostEffect::ColorGrading:        return 0.3f;   // LUT lookup
        case PostEffect::AmbientOcclusion:    return 2.4f;
        case PostEffect::ScreenSpaceReflections: return 3.5f;
        case PostEffect::ChromaticAberration: return 0.4f;
        case PostEffect::FilmGrain:           return 0.2f;
        case PostEffect::Vignette:            return 0.2f;
        case PostEffect::SpectralDispersion:  return 1.2f;   // N taps of the spectral LUT
        case PostEffect::Fx:                  return 0.8f;
    }
    return 0.0f;
}

i32 post_effect_min_tier(PostEffect e) {
    switch (e) {
        case PostEffect::None:
        case PostEffect::ColorGrading:
        case PostEffect::Vignette:            return 0;
        case PostEffect::FilmGrain:
        case PostEffect::ChromaticAberration:
        case PostEffect::Fx:
        case PostEffect::Bloom:               return 1;
        case PostEffect::SpectralDispersion:
        case PostEffect::MotionBlur:
        case PostEffect::AmbientOcclusion:    return 2;
        case PostEffect::DepthOfField:
        case PostEffect::ScreenSpaceReflections: return 3;
    }
    return 0;
}

void PostProcessStack::remove(PostEffect e) {
    i32 i = find(e);
    if (i >= 0) effects_.erase(effects_.begin() + i);
}

i32 PostProcessStack::find(PostEffect e) const {
    for (std::size_t i = 0; i < effects_.size(); ++i)
        if (effects_[i] == e) return static_cast<i32>(i);
    return -1;
}

void PostProcessStack::sort_canonical() {
    static const PostEffect kOrder[] = {
        PostEffect::AmbientOcclusion,
        PostEffect::ScreenSpaceReflections,
        PostEffect::DepthOfField,
        PostEffect::MotionBlur,
        PostEffect::SpectralDispersion,
        PostEffect::Bloom,
        PostEffect::ChromaticAberration,
        PostEffect::ColorGrading,
        PostEffect::FilmGrain,
        PostEffect::Vignette,
        PostEffect::Fx,
    };
    std::vector<PostEffect> out;
    out.reserve(effects_.size());
    for (PostEffect e : kOrder) if (contains(e)) out.push_back(e);
    // anything not in the canonical table keeps its relative order at the end
    auto in = [&](PostEffect e) {
        return std::find(out.begin(), out.end(), e) != out.end();
    };
    for (PostEffect e : effects_) if (!in(e)) out.push_back(e);
    effects_ = std::move(out);
}

std::size_t PostProcessStack::apply_budget(const GpuProfile& gpu, i32 quality_tier, f32 frame_budget_ms) {
    sort_canonical();
    std::vector<PostEffect> keep;
    f32 used = 0;
    std::size_t dropped = 0;
    for (PostEffect e : effects_) {
        const f32 cost = estimated_effect_ms(gpu, e, 1920, 1080);
        const bool tier_ok = post_effect_min_tier(e) <= quality_tier;
        const bool gpu_ok = !(e == PostEffect::ScreenSpaceReflections && !gpu.supports_deferred &&
                              gpu.tier < GpuTier::Flagship);
        if (tier_ok && gpu_ok && (used + cost) <= frame_budget_ms) {
            keep.push_back(e);
            used += cost;
        } else {
            ++dropped;
        }
    }
    effects_ = std::move(keep);
    return dropped;
}

f32 PostProcessStack::estimated_effect_ms(const GpuProfile& gpu, PostEffect e,
                                          i32 viewport_w, i32 viewport_h) {
    // Cost model: relative pass weight scaled by pixel count against a
    // 1080p reference, then divided by a rough fill-rate factor per GPU tier.
    const f64 pixels = static_cast<f64>(std::max(1, viewport_w)) * std::max(1, viewport_h);
    const f64 ref = 1920.0 * 1080.0;
    f32 fill = 1.0f;
    switch (gpu.tier) {
        case GpuTier::Potato:   fill = 4.0f;  break;
        case GpuTier::Low:      fill = 2.2f;  break;
        case GpuTier::Mid:      fill = 1.0f;  break;
        case GpuTier::High:     fill = 0.6f;  break;
        case GpuTier::Flagship: fill = 0.35f; break;
    }
    // A lower render scale means fewer pixels to shade.
    return post_effect_cost(e) * static_cast<f32>(pixels / ref) * fill;
}

f32 PostProcessStack::estimated_cost_ms(const GpuProfile& gpu, i32 viewport_w, i32 viewport_h) const {
    f32 total = 0;
    for (PostEffect e : effects_) total += estimated_effect_ms(gpu, e, viewport_w, viewport_h);
    return total;
}

// ============================================================== pipeline ====
RenderPipeline build_pipeline(const GpuProfile& gpu, i32 quality_tier, i32 width, i32 height) {
    RenderPipeline out;
    out.gpu = gpu;
    out.config.quality_tier = std::max(0, std::min(3, quality_tier));
    out.config.path = choose_render_path(gpu);
    out.config.backend = gpu.vulkan_1_3 ? Backend::Vulkan13
                        : (gpu.gles_3_0 || gpu.vulkan_1_1 ? Backend::Gles32 : Backend::Gles32);
    out.config.hdr = gpu.texture_float && out.config.quality_tier >= 1;
    out.config.msaa = gpu.tier >= GpuTier::High && !gpu.tile_based;   // TBDR prefers its own resolve
    out.config.msaa_samples = out.config.msaa ? 4 : 0;
    out.config.shadows = out.config.quality_tier >= 1;
    out.config.shadow_map_size = out.config.quality_tier >= 2 ? 2048 : 1024;
    out.config.gpu_skinning = true;
    out.config.gpu_driven = gpu.mesh_shaders;
    out.config.volumetric_fog = out.config.quality_tier >= 2 && gpu.compute_shaders;
    out.config.spectral = out.config.quality_tier >= 1;
    out.config.global_illumination = gpu.supports_sdfgi && out.config.quality_tier >= 3;
    out.config.ssr = gpu.tier >= GpuTier::Flagship && out.config.quality_tier >= 3;
    out.config.render_scale = gpu.suggested_render_scale();

    const i32 w = std::max(1, static_cast<i32>(width * out.config.render_scale));
    const i32 h = std::max(1, static_cast<i32>(height * out.config.render_scale));

    out.color_format = out.config.hdr ? TextureFormat::RGBA8 : gpu.preferred_format(false);
    out.depth_format = TextureFormat::None;

    FrameGraph fg;
    fg.declare(GraphResource{"scene_color", GraphResource::Kind::Color, w, h,
                             out.config.hdr ? TextureFormat::RGBA8 : TextureFormat::RGBA8});
    fg.declare(GraphResource{"scene_depth", GraphResource::Kind::Depth, w, h, TextureFormat::None});
    if (out.config.path == RenderPath::Deferred) {
        fg.declare(GraphResource{"gbuffer", GraphResource::Kind::Color, w, h, TextureFormat::RGBA8, 1, 4});
        fg.declare(GraphResource{"lighting", GraphResource::Kind::Color, w, h, TextureFormat::RGBA8});
    }
    fg.declare(GraphResource{"shadow_map", GraphResource::Kind::Depth, out.config.shadow_map_size,
                             out.config.shadow_map_size, TextureFormat::None});
    if (out.config.path == RenderPath::ForwardPlus)
        fg.declare(GraphResource{"light_grid", GraphResource::Kind::Buffer, 0, 0, TextureFormat::None});
    if (out.config.spectral)
        fg.declare(GraphResource{"spectral_lut", GraphResource::Kind::Buffer, 0, 0, TextureFormat::None});
    fg.declare(GraphResource{"bloom_chain", GraphResource::Kind::Color, w / 2, h / 2,
                             TextureFormat::RGBA8, 5});
    fg.declare(GraphResource{"swapchain", GraphResource::Kind::Color, width, height,
                             TextureFormat::RGBA8, 1, 1, false});

    auto pass = [&](const std::string& name, GraphPass::Kind kind,
                    std::vector<std::string> reads, std::vector<std::string> writes,
                    f32 cost, i32 min_tier = 0, bool optional = false) {
        GraphPass p;
        p.name = name; p.kind = kind;
        p.reads = std::move(reads); p.writes = std::move(writes);
        p.cost = cost; p.min_quality_tier = min_tier; p.optional = optional;
        fg.add(std::move(p));
    };

    if (out.config.shadows)
        pass("shadow_pass", GraphPass::Kind::Raster, {}, {"shadow_map"}, 1.5f, 1);
    if (out.config.path == RenderPath::ForwardPlus)
        pass("light_cull_compute", GraphPass::Kind::Compute, {"scene_depth"}, {"light_grid"}, 0.8f);
    if (out.config.spectral)
        pass("spectral_lut_build", GraphPass::Kind::Compute, {}, {"spectral_lut"}, 0.1f);

    switch (out.config.path) {
        case RenderPath::Deferred:
            pass("gbuffer_pass", GraphPass::Kind::Raster, {"shadow_map"}, {"gbuffer", "scene_depth"}, 3.0f);
            pass("lighting_resolve", GraphPass::Kind::Raster, {"gbuffer", "scene_depth"}, {"lighting"}, 2.0f);
            pass("transparent_pass", GraphPass::Kind::Raster, {"lighting", "scene_depth"}, {"scene_color"}, 1.5f);
            break;
        case RenderPath::ForwardPlus:
            pass("depth_prepass", GraphPass::Kind::Raster, {}, {"scene_depth"}, 1.2f, 1);
            pass("forward_plus_pass", GraphPass::Kind::Raster,
                 {"scene_depth", "light_grid", "shadow_map", "spectral_lut"}, {"scene_color"}, 3.2f);
            break;
        case RenderPath::Forward:
            pass("forward_pass", GraphPass::Kind::Raster, {"shadow_map", "spectral_lut"},
                 {"scene_color", "scene_depth"}, 2.6f);
            break;
        case RenderPath::MobileOptimized:
            pass("mobile_forward_pass", GraphPass::Kind::Raster, {"spectral_lut"},
                 {"scene_color", "scene_depth"}, 1.8f);
            break;
    }

    // Each stage writes its OWN resource. Reusing one name for a read-modify-
    // write would make two passes producers of the same target, which the
    // frame graph would (correctly) reject as a cycle.
    std::string colour = "scene_color";
    if (out.config.global_illumination) {
        pass("sdfgi", GraphPass::Kind::Compute, {colour, "scene_depth"}, {"scene_color_gi"}, 4.0f, 3, true);
        fg.declare(GraphResource{"scene_color_gi", GraphResource::Kind::Color, w, h, TextureFormat::RGBA8});
        colour = "scene_color_gi";
    }
    pass("post_bloom", GraphPass::Kind::Compute, {colour}, {"bloom_chain"}, 1.6f, 1, true);
    pass("post_spectral", GraphPass::Kind::Raster, {colour, "spectral_lut", "bloom_chain"},
         {"scene_color_post"}, 1.2f, 1, true);
    fg.declare(GraphResource{"scene_color_post", GraphResource::Kind::Color, w, h, TextureFormat::RGBA8});
    pass("tonemap_resolve", GraphPass::Kind::Raster, {"scene_color_post", "bloom_chain"}, {"swapchain"}, 0.5f);

    fg.compile("swapchain", out.config.quality_tier);
    out.passes.clear();
    for (const auto& p : fg.passes()) out.passes.push_back(p.name);
    out.transient_bytes = fg.peak_transient_bytes();
    out.estimated_frame_ms = fg.total_cost() * (gpu.tier <= GpuTier::Low ? 2.0f
                                                : gpu.tier == GpuTier::Mid ? 1.0f : 0.55f);
    out.viable = !out.passes.empty() && !fg.has_cycle();
    if (!out.viable) out.reason = fg.has_cycle() ? "frame graph cycle" : "no passes survived compilation";
    return out;
}

} // namespace prism::render
