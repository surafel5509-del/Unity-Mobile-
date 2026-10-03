// =====================================================================
//  PRISM ENGINE — assets/assets.h
//  The "One File" half of the tagline: a single self-describing .prism
//  container holds everything a build needs, addressed by content hash
//  so two identical assets are stored once and any tampering is visible.
//
//  Contents:
//    Guid           — 128-bit asset identity, stable across renames
//    PrismPack      — container: header, TOC, entries, SHA-256 manifest
//    AssetDatabase  — import registry, dependency graph, dirty tracking
//    ShelfAtlas     — sprite/glyph atlas rect packer
//    MeshData       — interleaved vertex + index buffers, bounds, tangents
//    etc1           — ETC2/ETC1 RGB8 block codec for the GLES fallback
//    MipChain       — box-filter mipmap generation
// =====================================================================
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::assets {

// ------------------------------------------------------------------ guid ---
struct Guid {
    u64 hi = 0, lo = 0;
    [[nodiscard]] bool null() const { return hi == 0 && lo == 0; }
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] static Guid from_string(const std::string& s);
    /// Deterministic guid for a path or logical name (not random).
    [[nodiscard]] static Guid deterministic(std::string_view seed);
    bool operator==(const Guid& o) const { return hi == o.hi && lo == o.lo; }
    bool operator<(const Guid& o) const { return hi != o.hi ? hi < o.hi : lo < o.lo; }
};
struct GuidHash { std::size_t operator()(const Guid& g) const { return static_cast<std::size_t>(g.hi * 0x9E3779B97F4A7C15ull ^ g.lo); } };

// -------------------------------------------------------------- container --
enum class EntryKind : u8 {
    Unknown = 0, Text, Json, Texture, Mesh, Audio, Animation, Font, Script,
    Scene, Material, Shader, Prefab, Particle, NavMesh, Localisation, Binary,
};
const char* entry_kind_name(EntryKind k);

struct PackEntry {
    Guid guid;
    std::string name;
    EntryKind kind = EntryKind::Binary;
    u64 offset = 0;                 // into the pack's data section
    u32 size = 0;
    u32 flags = 0;
    std::vector<Guid> dependencies;
    u8 digest[32] = {};             // SHA-256 of the payload
    [[nodiscard]] std::string digest_hex() const;
};

/// .prism file format, version 1.
///   magic "PRISM\0\1\0"  | u32 entry_count | u32 toc_bytes | u32 data_bytes
///   | TOC (binary)      | data section
/// The whole file is digest-stamped so a modified pack fails verification.
class PrismPack {
public:
    static constexpr u32 kVersion = 1;
    static constexpr u32 kMagic = 0x50524953u;         // "PRIS"

    void add(std::string name, EntryKind kind, std::vector<u8> payload, std::vector<Guid> deps = {});
    void add_text(std::string name, EntryKind kind, std::string_view text, std::vector<Guid> deps = {});
    [[nodiscard]] std::vector<u8> build() const;
    [[nodiscard]] bool load(const u8* data, std::size_t len, std::string* error = nullptr);

    [[nodiscard]] const std::vector<PackEntry>& entries() const { return entries_; }
    [[nodiscard]] const PackEntry* find(std::string_view name) const;
    [[nodiscard]] const PackEntry* find(const Guid& g) const;
    [[nodiscard]] std::vector<u8> payload(std::string_view name) const;
    [[nodiscard]] std::string text(std::string_view name) const;
    [[nodiscard]] std::size_t entry_count() const { return entries_.size(); }
    [[nodiscard]] bool verified() const { return verified_; }
    [[nodiscard]] std::string manifest_hex() const { return manifest_; }
    /// Re-hashes every payload against its recorded digest.
    [[nodiscard]] bool verify_integrity() const;
    /// Total stored bytes plus the duplicate bytes saved by deduplication.
    [[nodiscard]] u64 bytes_stored() const;
    [[nodiscard]] u64 bytes_deduplicated() const { return dedup_saved_; }

private:
    std::vector<PackEntry> entries_;
    std::unordered_map<std::string, std::vector<u8>> payloads_;
    bool verified_ = false;
    mutable std::string manifest_;
    u64 dedup_saved_ = 0;
};

// --------------------------------------------------------- asset database --
struct AssetRecord {
    Guid guid;
    std::string name;
    EntryKind kind = EntryKind::Binary;
    std::string digest_hex;
    u64 size = 0;
    bool dirty = false;
    std::vector<std::string> depends_on;
};

/// Tracks every imported asset, its content hash and its dependency edges so
/// a change to a shared material re-imports exactly the prefabs that use it.
class AssetDatabase {
public:
    /// Registers or updates an asset; returns false if the content is
    /// unchanged (nothing to rebuild).
    bool upsert(std::string name, EntryKind kind, std::string_view content,
                std::vector<std::string> deps = {});
    void remove(const std::string& name);
    [[nodiscard]] const AssetRecord* get(std::string_view name) const;
    [[nodiscard]] std::size_t size() const { return records_.size(); }
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] std::vector<std::string> dirty() const;
    void clear_dirty();
    /// Everything that (transitively) depends on `name`.
    [[nodiscard]] std::vector<std::string> reverse_dependencies(std::string_view name) const;
    /// Import order: dependencies before dependents. Empty on a cycle.
    [[nodiscard]] std::vector<std::string> build_order() const;
    [[nodiscard]] bool has_cycle() const;
    /// One string fingerprinting the whole database, for cache validation.
    [[nodiscard]] std::string fingerprint() const;
    [[nodiscard]] u64 total_bytes() const;

private:
    std::map<std::string, AssetRecord> records_;
};

// --------------------------------------------------------------- atlas -----
struct AtlasRect {
    u32 x = 0, y = 0, w = 0, h = 0;
    bool rotated = false;
    [[nodiscard]] bool valid() const { return w > 0 && h > 0; }
};

/// Shelf (next-fit decreasing height) packer. Deterministic, no allocation
/// churn, good enough for sprite sheets and runtime glyph atlases.
class ShelfAtlas {
public:
    ShelfAtlas(u32 width, u32 height, u32 padding = 1);
    AtlasRect insert(u32 w, u32 h);
    /// Convenience: pack a whole set, returning rects in input order.
    std::vector<AtlasRect> insert_all(const std::vector<std::pair<u32, u32>>& sizes);
    void reset();
    [[nodiscard]] u32 width() const { return width_; }
    [[nodiscard]] u32 height() const { return height_; }
    [[nodiscard]] u32 used_height() const { return cursor_y_; }
    [[nodiscard]] f32 occupancy() const;
    [[nodiscard]] u32 insert_count() const { return count_; }

private:
    u32 width_, height_, padding_;
    u32 cursor_x_ = 0, cursor_y_ = 0, shelf_h_ = 0;
    u32 used_area_ = 0, count_ = 0;
};

// ---------------------------------------------------------------- mesh -----
struct MeshVertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 uv;
    math::Vec4 tangent;               // xyz direction, w handedness
};

struct MeshData {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<u32> indices;
    math::Vec3 bounds_min, bounds_max;

    void compute_normals();
    void compute_tangents();
    void compute_bounds();
    void deduplicate_vertices();
    [[nodiscard]] math::Vec3 centre() const;
    [[nodiscard]] f32 radius() const;
    [[nodiscard]] u32 triangle_count() const { return static_cast<u32>(indices.size() / 3); }
    [[nodiscard]] std::vector<u8> to_bytes() const;
    [[nodiscard]] static bool from_bytes(const u8* data, std::size_t len, MeshData& out);
    /// Signed volume; negative means the winding is inside-out.
    [[nodiscard]] f32 signed_volume() const;
};

/// Standard primitives used by the samples and the editor gizmos.
namespace primitive {
[[nodiscard]] MeshData quad(f32 size = 1.0f);
[[nodiscard]] MeshData cube(f32 size = 1.0f);
[[nodiscard]] MeshData uv_sphere(f32 radius, u32 segments, u32 rings);
[[nodiscard]] MeshData cylinder(f32 radius, f32 height, u32 segments);
[[nodiscard]] MeshData grid(u32 cells, f32 cell_size);
} // namespace primitive

// ------------------------------------------------------- texture formats ---
enum class PixelFormat : u8 {
    R8 = 0, RG8, RGB8, RGBA8, R16F, RGBA16F, R32F, RGBA32F, ETC2_RGB8, ASTC_4x4,
};
const char* pixel_format_name(PixelFormat f);
/// Bits per pixel; block formats report the block cost amortised over its texels.
[[nodiscard]] u32 bits_per_pixel(PixelFormat f);
/// Bytes needed for a w x h image.
[[nodiscard]] std::size_t image_bytes(PixelFormat f, u32 w, u32 h);

struct Texture {
    std::string name;
    u32 width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    std::vector<u8> pixels;
    std::vector<std::vector<u8>> mips;
    bool generate_mips = true;

    [[nodiscard]] u32 mip_levels() const;
    void build_mip_chain();
    math::Color sample_nearest(f32 u, f32 v) const;      // RGBA8 only
    [[nodiscard]] f32 average_luminance() const;         // RGBA8 only
};

/// Box-filter mip chain for RGBA8. Each level halves both dimensions, with the
/// last level clamped at 1x1.
[[nodiscard]] std::vector<u8> box_downsample(const u8* src, u32 w, u32 h, u32& out_w, u32& out_h);

// ------------------------------------------------------------- ETC1/ETC2 ---
namespace etc1 {

/// The eight ETC modifier tables; each row is {small, large}.
extern const i32 kModifierTable[8][2];

/// Encode one 4x4 RGBA block (64 source bytes) into 8 bytes, individual mode
/// (diff bit clear), horizontal split, best-fit modifier table.
void encode_block(const u8* rgba, u8 out[8]);
/// Decode one 8-byte block back to 64 RGBA bytes. Handles both individual and
/// differential modes and both flip orientations.
void decode_block(const u8 block[8], u8 out_rgba[64]);

/// Encode/decode a whole image. Width and height must be multiples of 4.
[[nodiscard]] std::vector<u8> encode(const u8* rgba, u32 w, u32 h);
[[nodiscard]] std::vector<u8> decode(const u8* etc, u32 w, u32 h);
[[nodiscard]] std::size_t encoded_size(u32 w, u32 h);

/// Mean absolute per-channel error of a round trip, 0..255.
[[nodiscard]] f32 round_trip_error(const u8* rgba, u32 w, u32 h);
} // namespace etc1

} // namespace prism::assets
