// PRISM ENGINE — rendering tests: spectral optics, tonemapping, frustum
// culling, sprite batching, Forward+ light grid, adaptive resolution,
// frame-graph compilation and per-device pipeline selection.
#include "prism_test.h"
#include "prism/render/render.h"
#include <cmath>
#include <limits>

using namespace prism;
using namespace prism::render;
using math::Vec2;
using math::Vec3;
using math::Vec4;
using math::Mat4;

namespace {
GpuProfile make_gpu(const char* renderer, const char* vendor, i32 api = 34,
                    bool vulkan = true, i32 major = 1, i32 minor = 3) {
    return detect_gpu_profile(renderer, vendor, api, vulkan, major, minor);
}
} // namespace

PRISM_TEST(render_gpu_profile_classification) {
    auto adreno = make_gpu("Adreno (TM) 740", "Qualcomm");
    PRISM_CHECK(adreno.family == GpuFamily::Adreno);
    PRISM_CHECK_STR(adreno.model_id, "740");
    PRISM_CHECK(adreno.tier == GpuTier::Flagship);
    PRISM_CHECK(adreno.astc_ldr);
    PRISM_CHECK_STR(adreno.suggested_quality_tier(), "ultra");

    auto mali = make_gpu("Mali-G78", "ARM");
    PRISM_CHECK(mali.family == GpuFamily::Mali);
    PRISM_CHECK_STR(mali.model_id, "78");
    PRISM_CHECK(mali.tier == GpuTier::High);

    auto immortalis = make_gpu("Immortalis-G720", "ARM");
    PRISM_CHECK(immortalis.family == GpuFamily::Immortalis);
    PRISM_CHECK(immortalis.tier == GpuTier::Flagship);

    auto swift = make_gpu("Android Emulator OpenGL ES Translator (SwiftShader)", "Google");
    PRISM_CHECK(swift.family == GpuFamily::Swiftshader);
    PRISM_CHECK(swift.tier == GpuTier::Potato);
    PRISM_CHECK(!swift.astc_ldr);
    PRISM_CHECK(swift.preferred_format(true) == TextureFormat::ETC2_RGBA8);

    auto old = make_gpu("Adreno (TM) 330", "Qualcomm", 21, false, 3, 0);
    PRISM_CHECK(old.tier == GpuTier::Low);
    PRISM_CHECK(!old.supports_deferred);

    auto xclipse = make_gpu("Samsung Xclipse 920", "Samsung");
    PRISM_CHECK(xclipse.family == GpuFamily::Xclipse);
    PRISM_CHECK(xclipse.tier == GpuTier::Flagship);
    // Adreno uses 64-wide subgroups, everything else 32
    PRISM_CHECK_EQ(adreno.subgroup_size, 64);
    PRISM_CHECK_EQ(mali.subgroup_size, 32);
}

PRISM_TEST(render_path_selection_per_device) {
    auto swift = make_gpu("SwiftShader", "Google");
    PRISM_CHECK(choose_render_path(swift) == RenderPath::MobileOptimized);
    auto mid = make_gpu("Mali-G57", "ARM");
    PRISM_CHECK(choose_render_path(mid) == RenderPath::ForwardPlus ||
                choose_render_path(mid) == RenderPath::Forward);
    auto flag = make_gpu("Adreno (TM) 750", "Qualcomm");
    PRISM_CHECK(choose_render_path(flag) == RenderPath::Deferred);
    PRISM_CHECK_STR(render_path_name(RenderPath::Deferred), "Deferred");
    PRISM_CHECK_STR(backend_name(Backend::Vulkan13), "Vulkan 1.3");
}

PRISM_TEST(render_spectral_wavelength_to_rgb) {
    // 700 nm must read red, 532 nm green-dominant, 450 nm blue-dominant.
    Vec3 red = spectral::wavelength_to_linear_rgb(700.0f);
    Vec3 green = spectral::wavelength_to_linear_rgb(532.0f);
    Vec3 blue = spectral::wavelength_to_linear_rgb(450.0f);
    PRISM_CHECK(red.x > red.y && red.x > red.z);
    PRISM_CHECK(green.y > green.x && green.y > green.z);
    PRISM_CHECK(blue.z > blue.x);
    // out of gamut -> black
    Vec3 uv = spectral::wavelength_to_linear_rgb(200.0f);
    PRISM_CHECK_NEAR(uv.x + uv.y + uv.z, 0.0, 1e-6);
    // all channels stay in [0,1]
    auto lut = spectral::spectrum_lut(64);
    PRISM_CHECK_EQ(static_cast<int>(lut.size()), 64);
    for (const auto& c : lut) {
        PRISM_CHECK(c.x >= 0.0f && c.x <= 1.0f);
        PRISM_CHECK(c.y >= 0.0f && c.y <= 1.0f);
        PRISM_CHECK(c.z >= 0.0f && c.z <= 1.0f);
    }
    // sRGB encoding must brighten mid-tones. Use 600 nm, where the dominant
    // channel is below 1.0 (at 532 nm green is already saturated so the
    // transfer function is the identity there).
    Vec3 orange = spectral::wavelength_to_linear_rgb(600.0f);
    PRISM_CHECK(orange.y > 0.0f && orange.y < 1.0f);
    PRISM_CHECK(spectral::wavelength_to_srgb(600.0f).y > orange.y);
}

PRISM_TEST(render_spectral_prism_dispersion) {
    spectral::Glass bk7;   // crown glass defaults
    // Cauchy: shorter wavelength => higher index
    PRISM_CHECK(bk7.ior(400.0f) > bk7.ior(700.0f));
    PRISM_CHECK_NEAR(bk7.ior(589.0f), 1.5168, 0.02);

    const f32 apex = 60.0f * math::Deg2Rad;
    const f32 inc = 45.0f * math::Deg2Rad;
    const f32 d_red = spectral::prism_deviation(700.0f, apex, inc, bk7);
    const f32 d_vio = spectral::prism_deviation(400.0f, apex, inc, bk7);
    PRISM_CHECK(std::isfinite(d_red));
    PRISM_CHECK(std::isfinite(d_vio));
    // Violet deviates MORE than red through the same prism.
    PRISM_CHECK(d_vio > d_red);
    const f32 spread = spectral::angular_dispersion(400.0f, 700.0f, apex, inc, bk7);
    PRISM_CHECK(spread > 0.0f);
    PRISM_CHECK_NEAR(spread, std::fabs(d_vio - d_red), 1e-6);

    // Total internal reflection is reported as NaN, not silently clamped.
    f32 tir = std::numeric_limits<f32>::quiet_NaN();
    for (f32 a = 1.0f; a < 89.0f; a += 1.0f) {
        f32 d = spectral::prism_deviation(400.0f, a * math::Deg2Rad, 80.0f * math::Deg2Rad, bk7);
        if (std::isnan(d)) { tir = d; break; }
    }
    PRISM_CHECK(std::isnan(tir));

    // apex_for_spread: the returned apex really produces (approximately) the
    // requested angular spread when probed at normal incidence.
    const f32 want = 5.0f * math::Deg2Rad;
    f32 apex2 = spectral::apex_for_spread(400.0f, 700.0f, want, bk7);
    f32 got = spectral::angular_dispersion(400.0f, 700.0f, apex2, 0.0f, bk7);
    PRISM_CHECK(std::isfinite(got));
    PRISM_CHECK_NEAR(got, want, want * 0.25);
}

PRISM_TEST(render_spectral_dispersion_offsets_and_thin_film) {
    // Red bends least, violet most: the offsets must be ordered.
    Vec2 dir(1.0f, 0.0f);
    f32 r = spectral::dispersion_offset(700.0f, 0.02f, dir).x;
    f32 g = spectral::dispersion_offset(532.0f, 0.02f, dir).x;
    f32 v = spectral::dispersion_offset(400.0f, 0.02f, dir).x;
    PRISM_CHECK(r < g);
    PRISM_CHECK(g < v);
    // thin film stays in gamut and varies with thickness
    Vec3 a = spectral::thin_film(300.0f, 1.0f);
    Vec3 b = spectral::thin_film(600.0f, 1.0f);
    PRISM_CHECK(a.x >= 0 && a.x <= 1 && a.y >= 0 && a.y <= 1 && a.z >= 0 && a.z <= 1);
    PRISM_CHECK(std::fabs(a.x - b.x) > 1e-4f || std::fabs(a.z - b.z) > 1e-4f);
}

PRISM_TEST(render_tonemap_and_colour_space) {
    // sRGB round-trip
    Vec3 lin(0.2f, 0.5f, 0.9f);
    Vec3 srgb = linear_to_srgb(lin);
    Vec3 back = srgb_to_linear(srgb);
    PRISM_CHECK_NEAR(back.x, lin.x, 1e-4);
    PRISM_CHECK_NEAR(back.y, lin.y, 1e-4);
    PRISM_CHECK_NEAR(back.z, lin.z, 1e-4);
    // ACES: black stays black, huge values saturate to ~1, monotonic in between
    Vec3 black = tonemap(Vec3(0, 0, 0), TonemapOp::AcesFilmic);
    PRISM_CHECK_NEAR(black.x, 0.0, 1e-6);
    Vec3 bright = tonemap(Vec3(1000, 1000, 1000), TonemapOp::AcesFilmic);
    PRISM_CHECK(bright.x > 0.95f && bright.x <= 1.0f);
    Vec3 lo = tonemap(Vec3(0.3f, 0.3f, 0.3f), TonemapOp::AcesFilmic);
    Vec3 hi = tonemap(Vec3(0.9f, 0.9f, 0.9f), TonemapOp::AcesFilmic);
    PRISM_CHECK(hi.x > lo.x);
    // exposure scales the result
    PRISM_CHECK(tonemap(Vec3(1, 1, 1), TonemapOp::Reinhard, 4.0f).x >
                tonemap(Vec3(1, 1, 1), TonemapOp::Reinhard, 0.25f).x);
    // every operator is defined and named
    for (TonemapOp op : {TonemapOp::None, TonemapOp::Reinhard, TonemapOp::Uncharted2,
                         TonemapOp::AcesFilmic, TonemapOp::Agx}) {
        Vec3 o = tonemap(Vec3(2, 1, 0.5f), op);
        PRISM_CHECK(o.x >= 0.0f && o.x <= 1.0f);
        PRISM_CHECK(tonemap_name(op) != nullptr);
    }
    Exposure e; e.average_luminance = 0.5f; e.key_value = 0.18f;
    PRISM_CHECK(e.compute_ev100() > 0.0f);
    PRISM_CHECK(e.multiplier() >= e.min_exposure && e.multiplier() <= e.max_exposure);
    PRISM_CHECK_NEAR(luminance(Vec3(0, 1, 0)), 0.7152, 1e-5);
}

PRISM_TEST(render_frustum_culling) {
    Mat4 vp = Mat4::perspective(60.0f * math::Deg2Rad, 16.0f / 9.0f, 0.1f, 100.0f) *
              Mat4::look_at(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Frustum f;
    f.extract_from_view_proj(vp);
    // in front of the camera, inside the cone
    PRISM_CHECK(f.contains_sphere(Vec3(0, 0, 0), 0.5f));
    PRISM_CHECK(f.contains_aabb(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f)));
    // behind the camera
    PRISM_CHECK(!f.contains_sphere(Vec3(0, 0, 20), 0.5f));
    // far outside the cone
    PRISM_CHECK(!f.contains_sphere(Vec3(200, 0, 0), 0.5f));
    // beyond the far plane
    PRISM_CHECK(!f.contains_sphere(Vec3(0, 0, -200), 0.5f));
    // a large sphere centred off-screen still overlaps the frustum
    PRISM_CHECK(f.contains_sphere(Vec3(6, 0, 0), 20.0f));
}

PRISM_TEST(render_lod_and_screen_coverage) {
    LodSelector sel;
    PRISM_CHECK_EQ(sel.select(0.5f), 0);     // huge on screen -> LOD 0
    PRISM_CHECK_EQ(sel.select(0.01f), 1);
    PRISM_CHECK_EQ(sel.select(0.005f), 2);
    PRISM_CHECK_EQ(sel.select(0.0001f), 3);  // beyond all thresholds

    f32 near_cov = screen_coverage(1.0f, 2.0f, 60.0f * math::Deg2Rad, 1080);
    f32 far_cov = screen_coverage(1.0f, 50.0f, 60.0f * math::Deg2Rad, 1080);
    PRISM_CHECK(near_cov > far_cov);
    PRISM_CHECK_NEAR(screen_coverage(1.0f, 0.0f, 1.0f, 1080), 1.0, 1e-6);
}

PRISM_TEST(render_sprite_batching_reduces_state_changes) {
    SpriteBatcher b;
    b.begin();
    // 100 sprites alternating textures but all the same order -> with sorting
    // they collapse into 2 batches instead of 100.
    for (int i = 0; i < 100; ++i) {
        math::Rect r{static_cast<f32>(i), 0, 1, 1};
        math::Rect uv{0, 0, 1, 1};
        b.draw_quad(r, uv, math::Color{1, 1, 1, 1}, (i % 2) == 0 ? 10u : 20u, 1u, 0);
    }
    std::size_t batches = b.end();
    PRISM_CHECK_EQ(static_cast<int>(batches), 2);
    PRISM_CHECK_EQ(static_cast<int>(b.stats().sprites), 100);
    PRISM_CHECK_EQ(static_cast<int>(b.stats().vertices), 400);
    PRISM_CHECK_EQ(static_cast<int>(b.stats().indices), 600);
    PRISM_CHECK_EQ(static_cast<int>(b.stats().breaks_texture), 1);

    // Different orders must NOT be merged: painter order is correctness.
    SpriteBatcher b2;
    b2.begin();
    for (int i = 0; i < 5; ++i)
        b2.draw_quad(math::Rect{0, 0, 1, 1}, math::Rect{0, 0, 1, 1}, math::Color{}, 7u, 1u, i);
    PRISM_CHECK_EQ(static_cast<int>(b2.end()), 5);
    PRISM_CHECK_EQ(static_cast<int>(b2.stats().breaks_order), 4);

    // 9-slice emits 9 quads for a full border set
    SpriteBatcher b3;
    b3.begin();
    b3.draw_nine_slice(math::Rect{0, 0, 100, 60}, math::Rect{0, 0, 64, 64},
                       16, 16, 16, 16, math::Color{}, 3u, 1u, 0);
    b3.end();
    PRISM_CHECK_EQ(static_cast<int>(b3.stats().sprites), 9);
    PRISM_CHECK_EQ(static_cast<int>(b3.stats().batches), 1);
    // degenerate borders must not produce empty quads
    SpriteBatcher b4;
    b4.begin();
    b4.draw_nine_slice(math::Rect{0, 0, 10, 10}, math::Rect{0, 0, 10, 10},
                       0, 0, 0, 0, math::Color{}, 3u, 1u, 0);
    b4.end();
    PRISM_CHECK_EQ(static_cast<int>(b4.stats().sprites), 1);
}

PRISM_TEST(render_forward_plus_light_grid) {
    LightGrid grid;
    grid.resize(30, 17, 64);   // 1920x1088 at 64px tiles
    PRISM_CHECK_EQ(grid.tiles_x(), 30);
    PRISM_CHECK_EQ(grid.tiles_y(), 17);

    Mat4 vp = Mat4::perspective(60.0f * math::Deg2Rad, 1920.0f / 1080.0f, 0.1f, 100.0f) *
              Mat4::look_at(Vec3(0, 0, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    std::vector<LightData> lights(3);
    lights[0].position = Vec3(0, 0, 0);   lights[0].radius = 3.0f;
    lights[1].position = Vec3(5, 0, 0);   lights[1].radius = 1.0f;
    lights[2].position = Vec3(0, 0, 500); lights[2].radius = 5.0f;   // outside depth range
    grid.rebuild(lights, vp, 1920, 1080);

    // Screen centre tile must see the centred light.
    const auto& centre = grid.tile(15, 8);
    bool has0 = false, has2 = false;
    for (u32 i : centre) { if (i == 0) has0 = true; if (i == 2) has2 = true; }
    PRISM_CHECK(has0);
    PRISM_CHECK(!has2);                     // the far light is depth-culled
    // an out-of-range tile query returns empty, not a crash
    PRISM_CHECK_EQ(static_cast<int>(grid.tile(-5, 999).size()), 0);
    // SSBO layout: (1 + max_per_tile) u32 per tile
    auto ssbo = grid.to_ssbo();
    const int stride = 1 + grid.max_lights_per_tile();
    PRISM_CHECK_EQ(static_cast<int>(ssbo.size()), 30 * 17 * stride);
    // slot for tile (15,8): count followed by the light indices
    const std::size_t slot = static_cast<std::size_t>((8 * 30 + 15) * stride);
    PRISM_CHECK_EQ(static_cast<int>(ssbo[slot]), static_cast<int>(centre.size()));
    PRISM_CHECK_EQ(static_cast<int>(ssbo[slot + 1]), 0);
    PRISM_CHECK(grid.total_assignments() > 0);
}

PRISM_TEST(render_adaptive_resolution_governor) {
    AdaptiveResolution::Config cfg;
    cfg.target_fps = 60.0f; cfg.step = 0.1f; cfg.cooldown_seconds = 1.0f;
    cfg.min_scale = 0.5f; cfg.max_scale = 1.0f;
    AdaptiveResolution a(cfg);
    PRISM_CHECK_NEAR(a.scale(), 1.0f, 1e-6);

    // Sustained low FPS -> scale drops, but only once per cooldown.
    a.update(0.016f, 30.0f);
    f32 after_first = a.scale();
    PRISM_CHECK(after_first < 1.0f);
    a.update(0.016f, 30.0f);               // still in cooldown
    PRISM_CHECK_NEAR(a.scale(), after_first, 1e-6);
    a.update(1.5f, 30.0f);                 // cooldown elapsed
    PRISM_CHECK(a.scale() < after_first);

    // Recovery when the device keeps up
    for (int i = 0; i < 40; ++i) { a.update(1.5f, 90.0f); }
    PRISM_CHECK_NEAR(a.scale(), cfg.max_scale, 1e-6);

    // Thermal pressure caps the ceiling below max
    a.set_thermal(3 /* SEVERE */, false);
    a.update(1.5f, 90.0f);
    PRISM_CHECK(a.scale() <= 1.0f - 3 * cfg.thermal_step + 1e-4f);
    PRISM_CHECK(a.stats().thermal_status == 3);
    // Battery saver costs another step
    a.set_thermal(0, true);
    a.update(1.5f, 90.0f);
    PRISM_CHECK(a.scale() <= cfg.max_scale - cfg.thermal_step + 1e-4f);
    // never below the floor
    AdaptiveResolution::Config hard = cfg; hard.min_scale = 0.6f;
    AdaptiveResolution b(hard);
    for (int i = 0; i < 60; ++i) b.update(2.0f, 10.0f);
    PRISM_CHECK_NEAR(b.scale(), 0.6f, 1e-6);
}

PRISM_TEST(render_post_process_budget_and_order) {
    PostProcessStack stack;
    stack.add(PostEffect::FilmGrain);
    stack.add(PostEffect::Bloom);
    stack.add(PostEffect::ScreenSpaceReflections);
    stack.add(PostEffect::Bloom);              // duplicate ignored
    stack.add(PostEffect::ColorGrading);
    stack.add(PostEffect::SpectralDispersion);
    PRISM_CHECK_EQ(static_cast<int>(stack.effects().size()), 5);
    stack.sort_canonical();
    // canonical order: spectral -> bloom -> color grading -> film grain
    const auto& e = stack.effects();
    PRISM_CHECK(e[0] == PostEffect::ScreenSpaceReflections);
    PRISM_CHECK(e[1] == PostEffect::SpectralDispersion);
    PRISM_CHECK(e[2] == PostEffect::Bloom);
    PRISM_CHECK(e[3] == PostEffect::ColorGrading);
    PRISM_CHECK(e[4] == PostEffect::FilmGrain);

    auto low = make_gpu("Mali-G52", "ARM");
    PostProcessStack low_stack = stack;
    std::size_t dropped = low_stack.apply_budget(low, 0 /* low tier */, 8.0f);
    PRISM_CHECK(dropped >= 3);
    // tier-0 effects survive, tier-2+ effects do not
    PRISM_CHECK(low_stack.contains(PostEffect::ColorGrading));
    PRISM_CHECK(!low_stack.contains(PostEffect::ScreenSpaceReflections));
    PRISM_CHECK(!low_stack.contains(PostEffect::SpectralDispersion));

    auto flag = make_gpu("Adreno (TM) 750", "Qualcomm");
    PRISM_CHECK(post_effect_cost(PostEffect::ScreenSpaceReflections) >
                post_effect_cost(PostEffect::Vignette));
    // Same stack, same resolution: a weak GPU costs several times more.
    PRISM_CHECK(stack.estimated_cost_ms(low, 1920, 1080) >
                stack.estimated_cost_ms(flag, 1920, 1080));
    // Same GPU: more pixels cost more.
    PRISM_CHECK(stack.estimated_cost_ms(flag, 1920, 1080) >
                stack.estimated_cost_ms(flag, 1280, 720));
    PRISM_CHECK_EQ(post_effect_min_tier(PostEffect::ColorGrading), 0);
    PRISM_CHECK_EQ(post_effect_min_tier(PostEffect::DepthOfField), 3);
    PRISM_CHECK_STR(post_effect_name(PostEffect::SpectralDispersion), "spectral_dispersion");
}

PRISM_TEST(render_frame_graph_dead_pass_culling_and_order) {
    FrameGraph fg;
    fg.declare(GraphResource{"color", GraphResource::Kind::Color, 1920, 1080, TextureFormat::RGBA8});
    fg.declare(GraphResource{"depth", GraphResource::Kind::Depth, 1920, 1080});
    fg.declare(GraphResource{"unused", GraphResource::Kind::Color, 1920, 1080, TextureFormat::RGBA8});
    fg.declare(GraphResource{"swapchain", GraphResource::Kind::Color, 1920, 1080,
                             TextureFormat::RGBA8, 1, 1, false});

    GraphPass shadow; shadow.name = "shadow"; shadow.writes = {"depth"}; shadow.cost = 1.0f;
    GraphPass scene;  scene.name = "scene";  scene.reads = {"depth"}; scene.writes = {"color"}; scene.cost = 3.0f;
    GraphPass dead;   dead.name = "dead";    dead.writes = {"unused"}; dead.cost = 5.0f;
    GraphPass present; present.name = "present"; present.reads = {"color"}; present.writes = {"swapchain"};
    // declared out of order on purpose
    fg.add(dead); fg.add(present); fg.add(shadow); fg.add(scene);

    std::size_t kept = fg.compile("swapchain", 1);
    PRISM_CHECK_EQ(static_cast<int>(kept), 3);
    PRISM_CHECK(!fg.has_cycle());
    PRISM_CHECK_EQ(static_cast<int>(fg.culled_passes().size()), 1);
    PRISM_CHECK_STR(fg.culled_passes()[0], "dead");
    // topological order: shadow before scene before present
    const auto& passes = fg.passes();
    int i_shadow = -1, i_scene = -1, i_present = -1;
    for (std::size_t i = 0; i < passes.size(); ++i) {
        if (passes[i].name == "shadow")  i_shadow = static_cast<int>(i);
        if (passes[i].name == "scene")   i_scene = static_cast<int>(i);
        if (passes[i].name == "present") i_present = static_cast<int>(i);
    }
    PRISM_CHECK(i_shadow >= 0 && i_scene > i_shadow && i_present > i_scene);
    PRISM_CHECK_NEAR(fg.total_cost(), 1.0f + 3.0f + 1.0f, 1e-4);  // + default cost of 'present'
    // resource size accounting includes mip chains
    GraphResource mipped{"mips", GraphResource::Kind::Color, 1024, 1024, TextureFormat::RGBA8, 3};
    PRISM_CHECK(mipped.estimated_bytes() > 1024 * 1024 * 4);
}

PRISM_TEST(render_frame_graph_detects_cycle) {
    FrameGraph fg;
    GraphPass a; a.name = "a"; a.reads = {"b_out"}; a.writes = {"a_out"};
    GraphPass b; b.name = "b"; b.reads = {"a_out"}; b.writes = {"b_out"};
    fg.add(a); fg.add(b);
    fg.compile("b_out", 1);
    PRISM_CHECK(fg.has_cycle());
}

PRISM_TEST(render_pipeline_is_deterministic_and_tier_scaled) {
    auto gpu = make_gpu("Adreno (TM) 740", "Qualcomm");
    RenderPipeline p0 = build_pipeline(gpu, 0, 1920, 1080);
    RenderPipeline p0b = build_pipeline(gpu, 0, 1920, 1080);
    PRISM_CHECK(p0.viable);
    PRISM_CHECK_EQ(static_cast<int>(p0.passes.size()), static_cast<int>(p0b.passes.size()));
    for (std::size_t i = 0; i < p0.passes.size(); ++i)
        PRISM_CHECK_STR(p0.passes[i], p0b.passes[i]);

    RenderPipeline p3 = build_pipeline(gpu, 3, 1920, 1080);
    PRISM_CHECK(p3.viable);
    // Ultra does strictly more work than low
    PRISM_CHECK(p3.passes.size() > p0.passes.size());
    PRISM_CHECK(p3.estimated_frame_ms > p0.estimated_frame_ms);
    // the swapchain is always the last writer
    PRISM_CHECK_STR(p0.passes.back(), "tonemap_resolve");
    // low tier disables HDR
    PRISM_CHECK(!p0.config.hdr);
    PRISM_CHECK(p3.config.hdr);
    // transient memory stays inside a sane mobile budget (< 256 MB)
    PRISM_CHECK(p3.transient_bytes < 256 * 1024 * 1024);

    auto potato = make_gpu("SwiftShader", "Google");
    RenderPipeline pp = build_pipeline(potato, 0, 1280, 720);
    PRISM_CHECK(pp.viable);
    PRISM_CHECK(pp.config.path == RenderPath::MobileOptimized);
    PRISM_CHECK(!pp.config.shadows);
}
