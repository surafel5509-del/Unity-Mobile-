// =====================================================================
//  PRISM ENGINE — render/render.h
//  Android-only rendering core. Backend-agnostic: the Vulkan 1.3 primary
//  path and the OpenGL ES 3.2 fallback path share this frame description.
//
//  Contains:
//    * Spectral      — PRISM's signature: wavelength<->RGB, Cauchy dispersion,
//                      prism deviation, thin-film iridescence, rainbow ramps
//    * Tonemap       — ACES filmic / Reinhard / Uncharted2, exposure, sRGB
//    * FrameGraph    — pass/resource declaration, topological order,
//                      automatic dead-pass culling
//    * SpriteBatcher — 2D batching by (texture, shader, order) key
//    * Culling       — frustum extraction from VP, AABB/sphere tests,
//                      LOD selection, occlusion accounting
//    * LightGrid     — Forward+ 2D tile light list (compute-friendly layout)
//    * AdaptiveResolution — FPS-driven render-scale governor
//    * PostProcessStack   — ordered mobile post FX with cost model
// =====================================================================
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"
#include "../platform/android/gpu_profile.h"

namespace prism::render {

using namespace prism::platform;

enum class Backend : u8 { Vulkan13, Gles32, Null };
const char* backend_name(Backend b);

enum class RenderPath : u8 { MobileOptimized, Forward, ForwardPlus, Deferred };
const char* render_path_name(RenderPath p);
/// Chooses the best path a device can actually sustain.
[[nodiscard]] RenderPath choose_render_path(const GpuProfile& gpu);

// ============================================================ spectral ======
namespace spectral {

inline constexpr f32 kMinWavelengthNm = 380.0f;
inline constexpr f32 kMaxWavelengthNm = 780.0f;

/// Approximate CIE 1931 XYZ -> sRGB for a single wavelength. Returns linear
/// sRGB in [0,1]; values outside the gamut are clipped, not scaled, so the
/// hue stays true at the spectrum ends.
[[nodiscard]] math::Vec3 wavelength_to_linear_rgb(f32 nm);
/// Same, but gamma-encoded for direct display / debug visualisation.
[[nodiscard]] math::Vec3 wavelength_to_srgb(f32 nm);

/// Cauchy dispersion: n(lambda) = A + B/lambda^2. Real optical-glass fit.
struct Glass {
    f32 cauchy_a = 1.5046f;     // BK7 crown glass
    f32 cauchy_b = 4200.0f;     // nm^2
    [[nodiscard]] f32 ior(f32 nm) const { return cauchy_a + cauchy_b / (nm * nm); }
};
/// Deviation angle (radians) of a ray through a prism of apex angle `apex`
/// at incidence `incidence`, for wavelength `nm`.
[[nodiscard]] f32 prism_deviation(f32 nm, f32 apex_rad, f32 incidence_rad, const Glass& g = {});
/// Angular spread (radians) between two wavelengths through the same prism —
/// this is what makes the white beam fan out into a rainbow.
[[nodiscard]] f32 angular_dispersion(f32 nm_lo, f32 nm_hi, f32 apex_rad,
                                     f32 incidence_rad, const Glass& g = {});
/// Minimum-deviation apex angle that spreads `nm_lo..nm_hi` across `target_rad`.
[[nodiscard]] f32 apex_for_spread(f32 nm_lo, f32 nm_hi, f32 target_rad, const Glass& g = {});

/// Thin-film interference (soap bubble / oil slick / spectral chrome).
/// `thickness_nm` is the film thickness, `ior` its refractive index.
[[nodiscard]] math::Vec3 thin_film(f32 thickness_nm, f32 cos_theta, f32 ior = 1.4f);

/// Sample the visible spectrum into `bands` linear-RGB samples — the texture
/// the spectral post pass samples instead of doing per-pixel dispersion.
[[nodiscard]] std::vector<math::Vec3> spectrum_lut(i32 bands = 64);

/// Refraction offset (in UV space) for one wavelength, used by the
/// chromatic-dispersion post effect. Red bends least, violet most.
[[nodiscard]] math::Vec2 dispersion_offset(f32 nm, f32 strength, math::Vec2 direction);
} // namespace spectral

// ============================================================== tonemap =====
enum class TonemapOp : u8 { None, Reinhard, Uncharted2, AcesFilmic, Agx };
const char* tonemap_name(TonemapOp op);

struct Exposure {
    f32 key_value = 0.18f;
    f32 average_luminance = 0.2f;
    f32 min_exposure = 0.01f;
    f32 max_exposure = 100.0f;
    f32 adaptation_speed = 1.5f;      // stops per second
    f32 ev100 = 0;
    [[nodiscard]] f32 compute_ev100() const;
    [[nodiscard]] f32 multiplier() const;
};

[[nodiscard]] math::Vec3 tonemap(math::Vec3 hdr, TonemapOp op, f32 exposure = 1.0f);
[[nodiscard]] math::Vec3 linear_to_srgb(math::Vec3 linear);
[[nodiscard]] math::Vec3 srgb_to_linear(math::Vec3 srgb);
[[nodiscard]] f32 luminance(math::Vec3 linear);

// =========================================================== frame graph ===
/// A transient resource declared by a pass. The graph uses these to find
/// which passes nothing reads from and drop them before recording commands.
struct GraphResource {
    std::string name;
    enum class Kind : u8 { Color, Depth, Buffer, History } kind = Kind::Color;
    i32 width = 0, height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    i32 mip_levels = 1;
    i32 array_layers = 1;
    bool transient = true;
    /// Estimated bytes, used for the mobile memory budget check.
    [[nodiscard]] i64 estimated_bytes() const;
};

struct GraphPass {
    std::string name;
    enum class Kind : u8 { Raster, Compute, Copy, Present } kind = Kind::Raster;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    /// Relative GPU cost; the governor uses this to shed passes when thermal
    /// pressure rises. 0 = free, 100 = full-screen heavy post effect.
    f32 cost = 1.0f;
    i32  min_quality_tier = 0;        // 0=low .. 3=ultra
    bool optional = false;
    bool culled = false;
};

class FrameGraph {
public:
    void declare(GraphResource r);
    void add(GraphPass p);
    void clear();

    /// Resolve: topologically sort, then iteratively drop passes whose outputs
    /// are never consumed (unless they present or are mandatory).
    /// Returns the number of passes retained.
    std::size_t compile(const std::string& present_target = "swapchain",
                        i32 quality_tier = 1);

    [[nodiscard]] const std::vector<GraphPass>& passes() const { return order_; }
    [[nodiscard]] const GraphResource* resource(const std::string& name) const;
    [[nodiscard]] std::size_t resource_count() const { return resources_.size(); }
    [[nodiscard]] i64 peak_transient_bytes() const;
    [[nodiscard]] f32 total_cost() const;
    [[nodiscard]] bool has_cycle() const { return cycle_; }
    /// Names of passes dropped by compile() — surfaced in the profiler.
    [[nodiscard]] const std::vector<std::string>& culled_passes() const { return culled_; }

private:
    std::vector<GraphResource> resources_;
    std::vector<GraphPass> declared_;
    std::vector<GraphPass> order_;
    std::vector<std::string> culled_;
    bool cycle_ = false;
};

// ============================================================== culling =====
struct Frustum {
    // planes: x,y,z = normal, w = distance (n.p + d >= 0 is inside)
    math::Vec4 planes[6]{};
    void extract_from_view_proj(const math::Mat4& vp);
    [[nodiscard]] bool contains_sphere(math::Vec3 center, f32 radius) const;
    [[nodiscard]] bool contains_aabb(math::Vec3 min, math::Vec3 max) const;
};

struct CullStats {
    u32 total = 0, frustum_culled = 0, occlusion_culled = 0, distance_culled = 0;
    u32 drawn = 0, draw_calls = 0, triangles = 0, instances = 0;
    [[nodiscard]] u32 visible() const { return drawn; }
};

/// LOD selection with a smooth hysteresis band to avoid popping.
struct LodSelector {
    f32 screen_coverage_bias = 1.0f;
    std::vector<f32> thresholds = {0.02f, 0.008f, 0.002f};   // switch points
    [[nodiscard]] i32 select(f32 screen_coverage) const;
};

[[nodiscard]] f32 screen_coverage(f32 bounding_radius, f32 distance,
                                  f32 fov_y_rad, i32 viewport_height);

// ========================================================= sprite batch =====
struct SpriteVertex {
    math::Vec2 pos;
    math::Vec2 uv;
    math::Color color;
};

struct SpriteBatchKey {
    u64 texture = 0;
    u32 shader = 0;
    i32 order = 0;      // painter's order for 2D
    bool operator==(const SpriteBatchKey& o) const {
        return texture == o.texture && shader == o.shader && order == o.order;
    }
};
struct SpriteBatchKeyHash {
    std::size_t operator()(const SpriteBatchKey& k) const {
        return static_cast<std::size_t>((k.texture * 1000003u) ^ (k.shader * 7919u) ^
                                        (static_cast<u64>(k.order) << 32));
    }
};

struct SpriteBatchStats {
    u32 sprites = 0, batches = 0, vertices = 0, indices = 0;
    u32 breaks_texture = 0, breaks_shader = 0, breaks_order = 0;
};

/// Collects 2D quads, sorts them by (order, shader, texture) to minimise state
/// changes, then emits interleaved vertex/index buffers. On a TBDR GPU every
/// avoided state change is a real win, so the sort order is order-first.
class SpriteBatcher {
public:
    static constexpr std::size_t kMaxSprites = 4096;

    void begin();
    void draw_quad(const math::Rect& rect, const math::Rect& uv, const math::Color& tint,
                   u64 texture = 0, u32 shader = 0, i32 order = 0);
    /// 9-slice: stretches the middle, keeps the corners fixed.
    void draw_nine_slice(const math::Rect& rect, const math::Rect& src, f32 border_l, f32 border_r,
                         f32 border_t, f32 border_b, const math::Color& tint,
                         u64 texture = 0, u32 shader = 0, i32 order = 0);
    std::size_t end();

    [[nodiscard]] const std::vector<SpriteVertex>& vertices() const { return vertices_; }
    [[nodiscard]] const std::vector<u32>& indices() const { return indices_; }
    [[nodiscard]] const SpriteBatchStats& stats() const { return stats_; }
    [[nodiscard]] std::size_t batch_count() const { return batch_boundaries_.size(); }
    [[nodiscard]] const std::vector<std::size_t>& batch_boundaries() const { return batch_boundaries_; }

private:
    struct Quad { math::Rect rect, uv; math::Color tint; u64 tex; u32 shader; i32 order; };
    void emit(const Quad& q);

    std::vector<Quad> quads_;
    std::vector<SpriteVertex> vertices_;
    std::vector<u32> indices_;
    std::vector<std::size_t> batch_boundaries_;
    SpriteBatchStats stats_;
};

// ====================================================== Forward+ lights =====
struct LightData {
    math::Vec3 position;
    math::Vec3 color{1, 1, 1};
    f32 intensity = 1.0f;
    f32 radius = 10.0f;
    i32 kind = 0;      // 0 = point, 1 = spot
    math::Vec3 direction{0, 0, -1};
    f32 spot_cos_inner = 0.9f, spot_cos_outer = 0.8f;
};

/// 2D tile grid mapping screen tiles to the lights that affect them.
/// This is exactly the buffer a Forward+ compute pre-pass would fill; on
/// GLES 3.0 devices the same table is computed on the CPU instead.
class LightGrid {
public:
    void resize(i32 tiles_x, i32 tiles_y, i32 tile_pixels);
    void rebuild(const std::vector<LightData>& lights, const math::Mat4& view_proj,
                 i32 viewport_w, i32 viewport_h);
    [[nodiscard]] const std::vector<u32>& tile(i32 tx, i32 ty) const;
    [[nodiscard]] i32 tiles_x() const { return tiles_x_; }
    [[nodiscard]] i32 tiles_y() const { return tiles_y_; }
    [[nodiscard]] i32 tile_pixels() const { return tile_pixels_; }
    [[nodiscard]] i32 max_lights_per_tile() const { return max_per_tile_; }
    [[nodiscard]] i32 total_assignments() const { return assignments_; }
    /// Flatten to the SSBO layout the Forward+ shader expects.
    [[nodiscard]] std::vector<u32> to_ssbo() const;

private:
    static const std::vector<u32>& empty_list();
    i32 tiles_x_ = 0, tiles_y_ = 0, tile_pixels_ = 32, max_per_tile_ = 16, assignments_ = 0;
    std::vector<std::vector<u32>> tiles_;
};

// ====================================================== adaptive quality ====
struct AdaptiveStats {
    f32 fps = 0, target_fps = 60, render_scale = 1.0f;
    i32 adjustments = 0;
    bool battery_saver = false;
    i32 thermal_status = 0;      // Android ThermalStatus: 0..6
};

/// FPS-driven render scale governor. Hysteresis + cooldown prevent the
/// oscillation you get from naive "scale = fps/target" controllers.
class AdaptiveResolution {
public:
    struct Config {
        f32 target_fps = 60.0f;
        f32 min_scale = 0.5f;
        f32 max_scale = 1.0f;
        f32 step = 0.05f;
        f32 cooldown_seconds = 1.0f;
        f32 tolerance = 0.08f;         // +-8% is "on target"
        f32 thermal_step = 0.15f;      // extra drop per thermal level above none
    };

    AdaptiveResolution() = default;
    explicit AdaptiveResolution(const Config& c) : cfg_(c) {}
    /// Feed one frame. Returns the new render scale.
    f32 update(f32 dt, f32 fps);
    void set_thermal(i32 thermal_status, bool battery_saver);
    [[nodiscard]] f32 scale() const { return scale_; }
    [[nodiscard]] const AdaptiveStats& stats() const { return stats_; }
    [[nodiscard]] const Config& config() const { return cfg_; }

private:
    Config cfg_;
    f32 scale_ = 1.0f;
    f32 cooldown_ = 0;
    AdaptiveStats stats_;
};

// ========================================================== post process ====
enum class PostEffect : u8 {
    None, Bloom, DepthOfField, MotionBlur, ColorGrading, AmbientOcclusion,
    ScreenSpaceReflections, ChromaticAberration, FilmGrain, Vignette,
    SpectralDispersion, Fx
};
const char* post_effect_name(PostEffect e);
/// Relative GPU cost on a mid-range mobile GPU (1.0 = one full-screen pass).
[[nodiscard]] f32 post_effect_cost(PostEffect e);
/// Minimum quality tier that may enable this effect (0=low .. 3=ultra).
[[nodiscard]] i32 post_effect_min_tier(PostEffect e);

struct PostSettings {
    f32 bloom_threshold = 1.0f, bloom_intensity = 0.6f;
    f32 dof_focus = 10.0f, dof_aperture = 2.8f;
    f32 motion_blur_strength = 0.4f;
    f32 ao_radius = 0.5f, ao_intensity = 1.0f;
    f32 chromatic_strength = 0.004f;
    f32 grain_amount = 0.05f;
    f32 vignette_intensity = 0.25f;
    f32 dispersion_strength = 0.02f;    // PRISM spectral signature
    i32 spectral_bands = 12;            // cost/quality trade-off
    TonemapOp tonemap = TonemapOp::AcesFilmic;
    f32 exposure = 1.0f;
    f32 saturation = 1.0f, contrast = 1.0f;
};

class PostProcessStack {
public:
    void add(PostEffect e) { if (find(e) < 0) effects_.push_back(e); }
    void remove(PostEffect e);
    void clear() { effects_.clear(); }
    [[nodiscard]] i32 find(PostEffect e) const;
    [[nodiscard]] bool contains(PostEffect e) const { return find(e) >= 0; }
    [[nodiscard]] const std::vector<PostEffect>& effects() const { return effects_; }
    /// Reorders into the canonical order (AO -> SSR -> bloom -> tonemap-adjacent).
    void sort_canonical();
    /// Drops effects this device/tier cannot afford. Returns how many dropped.
    std::size_t apply_budget(const GpuProfile& gpu, i32 quality_tier, f32 frame_budget_ms = 8.0f);
    [[nodiscard]] f32 estimated_cost_ms(const GpuProfile& gpu, i32 viewport_w, i32 viewport_h) const;
    /// Milliseconds for one effect at this resolution on this GPU.
    [[nodiscard]] static f32 estimated_effect_ms(const GpuProfile& gpu, PostEffect e,
                                                 i32 viewport_w, i32 viewport_h);

    PostSettings& settings() { return settings_; }
    [[nodiscard]] const PostSettings& settings() const { return settings_; }

private:
    std::vector<PostEffect> effects_;
    PostSettings settings_;
};

// ============================================================== pipeline ====
struct PipelineConfig {
    Backend backend = Backend::Vulkan13;
    RenderPath path = RenderPath::ForwardPlus;
    bool hdr = true;
    bool msaa = false;
    i32 msaa_samples = 0;
    bool shadows = true;
    i32 shadow_map_size = 1024;
    bool gpu_skinning = true;
    bool gpu_driven = false;          // mesh shaders / indirect draws
    bool volumetric_fog = false;
    bool spectral = true;             // PRISM signature path
    bool global_illumination = false; // SDFGI / DDGI
    bool ssr = false;
    f32 render_scale = 1.0f;
    i32 quality_tier = 1;
};

/// Fully resolved renderer configuration for one device+quality combination.
struct RenderPipeline {
    PipelineConfig config;
    GpuProfile gpu;
    /// Passes in execution order, post-compilation (dead passes removed).
    std::vector<std::string> passes;
    i64 transient_bytes = 0;
    f32 estimated_frame_ms = 0;
    TextureFormat color_format = TextureFormat::RGBA8;
    TextureFormat depth_format = TextureFormat::None;
    bool viable = true;
    std::string reason;
};

/// Builds the pass list + resource table for a device. Deterministic.
[[nodiscard]] RenderPipeline build_pipeline(const GpuProfile& gpu, i32 quality_tier,
                                            i32 width, i32 height);

struct RenderStats {
    u64 frames = 0;
    f64 gpu_ms = 0, cpu_ms = 0;
    f32 fps = 0;
    CullStats cull;
    SpriteBatchStats sprites;
    i32 batches = 0, instances = 0;
    i64 transient_bytes = 0;
};

} // namespace prism::render
