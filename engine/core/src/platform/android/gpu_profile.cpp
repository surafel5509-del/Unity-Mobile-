// PRISM ENGINE — platform/android/gpu_profile.cpp
// Heuristic GPU classification from the strings the driver reports. Kept
// deliberately conservative: when we cannot recognise a device we assume
// ETC2 + Mid tier, which runs on every Android 8+ device with GLES 3.0.
#include "prism/platform/android/gpu_profile.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace prism::platform {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool contains(const std::string& hay, const char* needle) {
    return lower(hay).find(needle) != std::string::npos;
}
/// First run of digits found after `needle`, e.g. "adreno (tm) 740" -> 740.
std::string digits_after(const std::string& s, const char* needle) {
    auto l = lower(s);
    auto p = l.find(needle);
    if (p == std::string::npos) return {};
    p += std::string(needle).size();
    while (p < l.size() && !std::isdigit(static_cast<unsigned char>(l[p]))) ++p;
    std::size_t start = p;
    while (p < l.size() && std::isdigit(static_cast<unsigned char>(l[p]))) ++p;
    return l.substr(start, p - start);
}
} // namespace

const char* GpuProfile::family_name() const {
    switch (family) {
        case GpuFamily::Adreno:      return "Adreno";
        case GpuFamily::Mali:        return "Mali";
        case GpuFamily::Immortalis:  return "Immortalis";
        case GpuFamily::PowerVR:     return "PowerVR";
        case GpuFamily::Xclipse:     return "Xclipse";
        case GpuFamily::Apple:       return "Apple";
        case GpuFamily::Swiftshader: return "SwiftShader";
        case GpuFamily::Unknown:     break;
    }
    return "Unknown";
}

const char* GpuProfile::tier_name() const {
    switch (tier) {
        case GpuTier::Potato:   return "potato";
        case GpuTier::Low:      return "low";
        case GpuTier::Mid:      return "mid";
        case GpuTier::High:     return "high";
        case GpuTier::Flagship: return "flagship";
    }
    return "mid";
}

TextureFormat GpuProfile::preferred_format(bool has_alpha, bool normal_map) const {
    // ASTC is strongly preferred where supported: better quality per bit and
    // normal maps survive quantisation better than ETC2.
    if (astc_ldr) {
        // Normal maps get a denser block to keep the 2-channel gradient clean.
        if (normal_map) return TextureFormat::ASTC_4x4;
        return has_alpha ? TextureFormat::ASTC_6x6 : TextureFormat::ASTC_6x6;
    }
    if (etc2) return has_alpha ? TextureFormat::ETC2_RGBA8 : TextureFormat::ETC2_RGB8;
    if (atc)  return has_alpha ? TextureFormat::ATC_RGBA : TextureFormat::ATC_RGB;
    return TextureFormat::RGBA8;
}

f32 GpuProfile::bits_per_pixel(TextureFormat f) {
    switch (f) {
        case TextureFormat::RGBA8:      return 32.0f;
        case TextureFormat::ETC2_RGBA8: return 8.0f;
        case TextureFormat::ETC2_RGB8:  return 4.0f;
        case TextureFormat::ASTC_4x4:   return 8.0f;
        case TextureFormat::ASTC_6x6:   return 128.0f / 36.0f;   // ~3.56
        case TextureFormat::ASTC_8x8:   return 2.0f;
        case TextureFormat::ATC_RGB:    return 4.0f;
        case TextureFormat::ATC_RGBA:   return 8.0f;
        case TextureFormat::DXT5:       return 8.0f;
        case TextureFormat::None:       break;
    }
    return 0.0f;
}

std::string GpuProfile::suggested_quality_tier() const {
    switch (tier) {
        case GpuTier::Potato:   return "low";
        case GpuTier::Low:      return "low";
        case GpuTier::Mid:      return "medium";
        case GpuTier::High:     return "high";
        case GpuTier::Flagship: return "ultra";
    }
    return "medium";
}

f32 GpuProfile::suggested_render_scale() const {
    switch (tier) {
        case GpuTier::Potato:   return 0.55f;
        case GpuTier::Low:      return 0.70f;
        case GpuTier::Mid:      return 0.85f;
        case GpuTier::High:     return 1.0f;
        case GpuTier::Flagship: return 1.0f;
    }
    return 0.85f;
}

const char* texture_format_name(TextureFormat f) {
    switch (f) {
        case TextureFormat::None:       return "none";
        case TextureFormat::RGBA8:      return "rgba8";
        case TextureFormat::ETC2_RGBA8: return "etc2_rgba8";
        case TextureFormat::ETC2_RGB8:  return "etc2_rgb8";
        case TextureFormat::ASTC_4x4:   return "astc_4x4";
        case TextureFormat::ASTC_6x6:   return "astc_6x6";
        case TextureFormat::ASTC_8x8:   return "astc_8x8";
        case TextureFormat::ATC_RGB:    return "atc_rgb";
        case TextureFormat::ATC_RGBA:   return "atc_rgba";
        case TextureFormat::DXT5:       return "dxt5";
    }
    return "none";
}

const char* texture_format_extension(TextureFormat f) {
    switch (f) {
        case TextureFormat::ASTC_4x4:
        case TextureFormat::ASTC_6x6:
        case TextureFormat::ASTC_8x8:   return ".astc";
        case TextureFormat::ETC2_RGBA8:
        case TextureFormat::ETC2_RGB8:  return ".ktx";
        case TextureFormat::ATC_RGB:
        case TextureFormat::ATC_RGBA:   return ".atc";
        case TextureFormat::DXT5:       return ".dds";
        case TextureFormat::RGBA8:
        case TextureFormat::None:       break;
    }
    return ".png";
}

bool family_prefers_astc(GpuFamily f) {
    switch (f) {
        case GpuFamily::Adreno:
        case GpuFamily::Mali:
        case GpuFamily::Immortalis:
        case GpuFamily::PowerVR:
        case GpuFamily::Xclipse:
        case GpuFamily::Apple:
            return true;
        case GpuFamily::Swiftshader:
        case GpuFamily::Unknown:
            return false;
    }
    return false;
}

GpuProfile detect_gpu_profile(const std::string& renderer,
                              const std::string& vendor,
                              i32 api_level,
                              bool vulkan,
                              i32 major_version,
                              i32 minor_version) {
    GpuProfile p;
    p.renderer = renderer.empty() ? "unknown" : renderer;
    p.vendor = vendor.empty() ? "unknown" : vendor;
    p.api_level = api_level;

    const std::string r = lower(renderer);
    const std::string v = lower(vendor);

    if (contains(r, "swiftshader") || contains(r, "llvmpipe") || contains(r, "software")) {
        p.family = GpuFamily::Swiftshader;
        p.tier = GpuTier::Potato;
    } else if (contains(r, "adreno") || contains(v, "qualcomm")) {
        p.family = GpuFamily::Adreno;
        p.model_id = digits_after(r, "adreno");
    } else if (contains(r, "immortalis")) {
        p.family = GpuFamily::Immortalis;
        p.model_id = digits_after(r, "immortalis");
    } else if (contains(r, "mali") || contains(v, "arm")) {
        p.family = GpuFamily::Mali;
        p.model_id = digits_after(r, "mali");
    } else if (contains(r, "powervr") || contains(r, "imagination") || contains(v, "imagination")) {
        p.family = GpuFamily::PowerVR;
        p.model_id = digits_after(r, "powervr");
    } else if (contains(r, "xclipse") || contains(r, "samsung")) {
        p.family = GpuFamily::Xclipse;
        p.model_id = digits_after(r, "xclipse");
    } else if (contains(r, "apple")) {
        p.family = GpuFamily::Apple;
    }

    // ---- API capabilities ------------------------------------------------
    p.vulkan_1_1 = vulkan && (major_version > 1 || (major_version == 1 && minor_version >= 1));
    p.vulkan_1_3 = vulkan && (major_version > 1 || (major_version == 1 && minor_version >= 3));
    p.gles_3_0 = !vulkan && (major_version >= 3);
    p.gles_3_2 = !vulkan && (major_version > 3 || (major_version == 3 && minor_version >= 2));
    p.compute_shaders = p.vulkan_1_1 || p.gles_3_2 || p.gles_3_0;
    // ETC2 is mandatory from GLES 3.0 / any Android Vulkan implementation.
    p.etc2 = p.gles_3_0 || p.gles_3_2 || vulkan || api_level >= 26;
    // ASTC LDR: Adreno 4xx+, Mali-T6xx+, PowerVR GX/GE, Xclipse. Conservative here.
    switch (p.family) {
        case GpuFamily::Adreno:      p.astc_ldr = true; break;
        case GpuFamily::Mali:        p.astc_ldr = !p.model_id.empty(); break;
        case GpuFamily::Immortalis:  p.astc_ldr = true; break;
        case GpuFamily::PowerVR:     p.astc_ldr = true; break;
        case GpuFamily::Xclipse:     p.astc_ldr = true; break;
        case GpuFamily::Apple:       p.astc_ldr = true; break;
        case GpuFamily::Swiftshader: p.astc_ldr = false; break;
        case GpuFamily::Unknown:     p.astc_ldr = false; break;
    }
    // ATC only ever existed on pre-GLES3 Adreno parts.
    p.atc = (p.family == GpuFamily::Adreno) && !p.etc2;

    // ---- tier from family + model number ---------------------------------
    i32 model = p.model_id.empty() ? 0 : std::atoi(p.model_id.c_str());
    switch (p.family) {
        case GpuFamily::Swiftshader: p.tier = GpuTier::Potato; break;
        case GpuFamily::Adreno:
            if (model >= 700)      p.tier = GpuTier::Flagship;
            else if (model >= 600) p.tier = GpuTier::High;
            else if (model >= 500) p.tier = GpuTier::Mid;
            else                   p.tier = GpuTier::Low;
            break;
        case GpuFamily::Immortalis:  p.tier = GpuTier::Flagship; break;
        case GpuFamily::Mali:
            // Arm renamed twice: T6xx/T8xx, then G5x/G7x, then G7xx (G710+).
            // Treat the 70-series and the 700-series as high-end, 50-series mid.
            if (model >= 700 || (model >= 70 && model < 100)) p.tier = GpuTier::High;
            else if (model >= 50)  p.tier = GpuTier::Mid;
            else                   p.tier = GpuTier::Low;
            break;
        case GpuFamily::Xclipse:
            p.tier = model >= 900 ? GpuTier::Flagship : GpuTier::High;
            break;
        case GpuFamily::PowerVR:     p.tier = GpuTier::Mid; break;
        case GpuFamily::Apple:       p.tier = GpuTier::High; break;
        case GpuFamily::Unknown:     p.tier = GpuTier::Mid; break;
    }
    // Very old Android cannot sustain anything above "low".
    if (api_level < 24 && p.tier > GpuTier::Low) p.tier = GpuTier::Low;

    // ---- render-path viability -------------------------------------------
    // TBDR GPUs pay heavily for full-screen bandwidth; deferred needs 3+ MRT
    // attachments plus a lighting resolve, which only high tiers can afford.
    p.tile_based = p.family != GpuFamily::Swiftshader;
    p.supports_deferred = (p.tier >= GpuTier::High) && p.max_color_attachments >= 4;
    p.supports_forward_plus = (p.tier >= GpuTier::Mid) && p.compute_shaders;
    p.supports_sdfgi = (p.tier == GpuTier::Flagship) && p.compute_shaders;
    p.mesh_shaders = false;      // not exposed on Android Vulkan 1.3 in practice
    p.ray_query = false;
    p.anisotropic_16x = p.tier >= GpuTier::Mid;
    p.texture_float = p.compute_shaders;
    p.max_texture_size = p.tier >= GpuTier::High ? 8192 : 4096;
    p.max_color_attachments = p.supports_deferred ? 4 : (p.tier >= GpuTier::Mid ? 2 : 1);
    p.subgroup_size = (p.family == GpuFamily::Adreno) ? 64 : 32;
    p.compute_shared_memory_kb = p.tier >= GpuTier::High ? 64 : 32;
    return p;
}

} // namespace prism::platform
