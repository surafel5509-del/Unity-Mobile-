// =====================================================================
//  PRISM ENGINE — platform/android/gpu_profile.h
//  Device capability detection for Android GPUs. Everything is derived
//  from the strings Vulkan/GLES report at runtime — no network lookup,
//  no device database download. The profile drives:
//    * texture compression choice (ASTC / ETC2 / ATC)
//    * default quality tier and render scale
//    * which render path is viable (Forward+ / Deferred / Forward / Mobile)
//    * GPU-driven vs CPU-driven rendering
// =====================================================================
#pragma once
#include <string>
#include <vector>
#include "../../core/types.h"

namespace prism::platform {

enum class GpuFamily : u8 {
    Unknown,
    Adreno,      // Qualcomm
    Mali,        // Arm
    Immortalis,  // Arm flagship (was Mali-G7xx Immortalis)
    PowerVR,     // Imagination / some MediaTek
    Xclipse,     // Samsung (AMD RDNA2)
    Apple,       // present only when cross-compiling reference data
    Swiftshader, // software rasterizer (emulator) — force lowest tier
};

enum class GpuTier : u8 { Potato, Low, Mid, High, Flagship };

/// Texture compression families available on Android.
enum class TextureFormat : u8 {
    None,
    RGBA8,          // uncompressed fallback
    ETC2_RGBA8,     // mandatory on GLES 3.0 / Vulkan Android
    ETC2_RGB8,
    ASTC_4x4,       // best quality, ~8 bpp
    ASTC_6x6,       // balanced, ~3.56 bpp  (PRISM default)
    ASTC_8x8,       // small, ~2 bpp
    ATC_RGB,        // legacy Adreno (GLES 2 era)
    ATC_RGBA,
    DXT5,           // x86 emulator only
};

struct GpuProfile {
    GpuFamily family = GpuFamily::Unknown;
    GpuTier tier = GpuTier::Mid;
    std::string renderer = "unknown";
    std::string vendor = "unknown";
    std::string model_id;            // e.g. "740" for Adreno 740

    // Feature bits filled from the API-reported extension/version list.
    bool vulkan_1_3 = false;
    bool vulkan_1_1 = false;
    bool gles_3_2 = false;
    bool gles_3_0 = false;
    bool astc_ldr = false;
    bool etc2 = false;
    bool atc = false;
    bool anisotropic_16x = false;
    bool texture_float = false;
    bool compute_shaders = false;
    bool mesh_shaders = false;          // rare on mobile; enables GPU-driven path
    bool ray_query = false;             // rare; enables hardware-accurate SSR/GI probe
    bool tile_based = true;             // nearly all mobile GPUs are TBDR/TBR
    bool supports_deferred = false;     // needs MRT + enough bandwidth
    bool supports_forward_plus = false;
    bool supports_sdfgi = false;        // needs compute + fp16 storage
    i32  max_texture_size = 4096;
    i32  max_color_attachments = 4;
    i32  subgroup_size = 32;
    i32  compute_shared_memory_kb = 32;
    i64  dedicated_video_memory_mb = 0;   // usually 0: UMA on mobile
    i32  api_level = 26;

    /// Best compression for this GPU, given an alpha requirement.
    [[nodiscard]] TextureFormat preferred_format(bool has_alpha, bool normal_map = false) const;
    [[nodiscard]] const char* family_name() const;
    [[nodiscard]] const char* tier_name() const;
    /// Suggested default quality tier string ("low".."ultra").
    [[nodiscard]] std::string suggested_quality_tier() const;
    /// Suggested render scale in [0.5, 1.0].
    [[nodiscard]] f32 suggested_render_scale() const;
    /// Bytes per pixel for the given format (0 = unknown).
    [[nodiscard]] static f32 bits_per_pixel(TextureFormat f);
};

/// Builds a profile from GL/Vulkan reported strings + API level.
/// Pure function: the same inputs always give the same profile, so quality
/// decisions are reproducible across runs (important for determinism tests).
[[nodiscard]] GpuProfile detect_gpu_profile(const std::string& renderer,
                                            const std::string& vendor,
                                            i32 api_level,
                                            bool vulkan,
                                            i32 major_version = 1,
                                            i32 minor_version = 3);

[[nodiscard]] const char* texture_format_name(TextureFormat f);
[[nodiscard]] const char* texture_format_extension(TextureFormat f);

/// Which families need an ETC2 fallback even when ASTC is present.
[[nodiscard]] bool family_prefers_astc(GpuFamily f);

} // namespace prism::platform
