// =====================================================================
//  PRISM ENGINE — assets/assets.cpp
//  The .prism container, the import database, atlas packing, meshes and
//  the ETC1/ETC2 block codec used on the GLES fallback path.
// =====================================================================
#include "prism/assets/assets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "prism/core/hash.h"

namespace prism::assets {

namespace {

// -------------------------------------------------- little binary helpers --
void put_u8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void put_u16(std::vector<u8>& v, u16 x) { v.push_back(static_cast<u8>(x & 0xFF)); v.push_back(static_cast<u8>(x >> 8)); }
void put_u32(std::vector<u8>& v, u32 x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF)); }
/// Magics are written big-endian so the file starts with readable ASCII.
void put_magic(std::vector<u8>& v, u32 x) { for (int i = 3; i >= 0; --i) v.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF)); }
void put_u64(std::vector<u8>& v, u64 x) { for (int i = 0; i < 8; ++i) v.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF)); }

class Cursor {
public:
    Cursor(const u8* d, std::size_t n) : d_(d), n_(n) {}
    bool need(std::size_t k) const { return ok_ && pos_ + k <= n_; }
    u32 read_magic() { u32 v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | read_u8(); return v; }
    u8 read_u8() { if (!need(1)) { ok_ = false; return 0; } return d_[pos_++]; }
    u16 read_u16() { u16 v = 0; for (int i = 0; i < 2; ++i) v |= static_cast<u16>(read_u8()) << (8 * i); return v; }
    u32 read_u32() { u32 v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<u32>(read_u8()) << (8 * i); return v; }
    u64 read_u64() { u64 v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<u64>(read_u8()) << (8 * i); return v; }
    std::string str(std::size_t len) {
        if (!need(len)) { ok_ = false; return {}; }
        std::string s(reinterpret_cast<const char*>(d_ + pos_), len);
        pos_ += len;
        return s;
    }
    const u8* raw(std::size_t len) { if (!need(len)) { ok_ = false; return nullptr; } const u8* p = d_ + pos_; pos_ += len; return p; }
    void skip(std::size_t k) { if (!need(k)) { ok_ = false; return; } pos_ += k; }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::size_t pos() const { return pos_; }
private:
    const u8* d_; std::size_t n_, pos_ = 0; bool ok_ = true;
};

std::string to_hex(const u8* data, std::size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.resize(len * 2);
    for (std::size_t i = 0; i < len; ++i) { s[i * 2] = kHex[data[i] >> 4]; s[i * 2 + 1] = kHex[data[i] & 0xF]; }
    return s;
}

inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

// =================================================================== guid ==
std::string Guid::to_string() const {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llx-%016llx",
                  static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
    return buf;
}

Guid Guid::from_string(const std::string& s) {
    Guid g;
    unsigned long long a = 0, b = 0;
    if (std::sscanf(s.c_str(), "%16llx-%16llx", &a, &b) == 2) { g.hi = a; g.lo = b; }
    else if (std::sscanf(s.c_str(), "%16llx%16llx", &a, &b) == 2) { g.hi = a; g.lo = b; }
    return g;
}

Guid Guid::deterministic(std::string_view seed) {
    const crypto::Sha256Digest d = crypto::Sha256::hash(seed);
    Guid g;
    for (int i = 0; i < 8; ++i) {
        g.hi = (g.hi << 8) | d[static_cast<std::size_t>(i)];
        g.lo = (g.lo << 8) | d[static_cast<std::size_t>(i + 8)];
    }
    return g;
}

// =================================================================== pack ==
const char* entry_kind_name(EntryKind k) {
    switch (k) {
        case EntryKind::Text: return "text";
        case EntryKind::Json: return "json";
        case EntryKind::Texture: return "texture";
        case EntryKind::Mesh: return "mesh";
        case EntryKind::Audio: return "audio";
        case EntryKind::Animation: return "animation";
        case EntryKind::Font: return "font";
        case EntryKind::Script: return "script";
        case EntryKind::Scene: return "scene";
        case EntryKind::Material: return "material";
        case EntryKind::Shader: return "shader";
        case EntryKind::Prefab: return "prefab";
        case EntryKind::Particle: return "particle";
        case EntryKind::NavMesh: return "navmesh";
        case EntryKind::Localisation: return "localisation";
        case EntryKind::Binary: return "binary";
        case EntryKind::Unknown: break;
    }
    return "unknown";
}

std::string PackEntry::digest_hex() const { return to_hex(digest, sizeof(digest)); }

void PrismPack::add(std::string name, EntryKind kind, std::vector<u8> payload, std::vector<Guid> deps) {
    PackEntry e;
    e.guid = Guid::deterministic(name);
    e.name = name;
    e.kind = kind;
    e.size = static_cast<u32>(payload.size());
    e.dependencies = std::move(deps);
    const crypto::Sha256Digest d = crypto::Sha256::hash(
        std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
    std::memcpy(e.digest, d.data(), 32);

    // Content addressing: an identical payload is stored once.
    const std::string key = e.digest_hex();
    if (payloads_.count(key)) {
        dedup_saved_ += payload.size();
    } else {
        payloads_[key] = std::move(payload);
    }
    entries_.push_back(std::move(e));
}

void PrismPack::add_text(std::string name, EntryKind kind, std::string_view text, std::vector<Guid> deps) {
    add(std::move(name), kind, std::vector<u8>(text.begin(), text.end()), std::move(deps));
}

std::vector<u8> PrismPack::build() const {
    std::vector<u8> toc;
    std::vector<u8> data;
    for (PackEntry e : entries_) {
        const std::string key = e.digest_hex();
        auto it = payloads_.find(key);
        if (it == payloads_.end()) continue;
        e.offset = data.size();
        e.size = static_cast<u32>(it->second.size());
        data.insert(data.end(), it->second.begin(), it->second.end());

        put_u64(toc, e.guid.hi);
        put_u64(toc, e.guid.lo);
        put_u8(toc, static_cast<u8>(e.kind));
        put_u32(toc, e.flags);
        put_u32(toc, e.size);
        put_u64(toc, e.offset);
        put_u16(toc, static_cast<u16>(e.name.size()));
        toc.insert(toc.end(), e.name.begin(), e.name.end());
        put_u16(toc, static_cast<u16>(e.dependencies.size()));
        for (const auto& d : e.dependencies) { put_u64(toc, d.hi); put_u64(toc, d.lo); }
        toc.insert(toc.end(), e.digest, e.digest + 32);
    }

    std::vector<u8> out;
    out.reserve(20 + toc.size() + data.size());
    put_magic(out, kMagic);
    put_u32(out, kVersion);
    put_u32(out, static_cast<u32>(entries_.size()));
    put_u32(out, static_cast<u32>(toc.size()));
    put_u32(out, static_cast<u32>(data.size()));
    out.insert(out.end(), toc.begin(), toc.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

bool PrismPack::load(const u8* data, std::size_t len, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    Cursor c(data, len);
    if (c.read_magic() != kMagic) return fail("not a .prism file (bad magic)");
    const u32 version = c.read_u32();
    if (version != kVersion) return fail("unsupported .prism version");
    const u32 count = c.read_u32();
    const u32 toc_bytes = c.read_u32();
    const u32 data_bytes = c.read_u32();
    if (!c.need(static_cast<std::size_t>(toc_bytes) + data_bytes)) return fail("truncated pack");
    if (count > 100000) return fail("implausible entry count");

    entries_.clear();
    payloads_.clear();
    dedup_saved_ = 0;

    const std::size_t toc_start = c.pos();
    for (u32 i = 0; i < count && c.ok(); ++i) {
        PackEntry e;
        e.guid.hi = c.read_u64();
        e.guid.lo = c.read_u64();
        e.kind = static_cast<EntryKind>(c.read_u8());
        e.flags = c.read_u32();
        e.size = c.read_u32();
        e.offset = c.read_u64();
        const u16 name_len = c.read_u16();
        e.name = c.str(name_len);
        const u16 deps = c.read_u16();
        for (u16 d = 0; d < deps; ++d) {
            Guid g;
            g.hi = c.read_u64();
            g.lo = c.read_u64();
            e.dependencies.push_back(g);
        }
        const u8* digest = c.raw(32);
        if (!digest) return fail("truncated table of contents");
        std::memcpy(e.digest, digest, 32);
        entries_.push_back(std::move(e));
    }
    if (!c.ok()) return fail("malformed table of contents");
    if (c.pos() - toc_start > toc_bytes) return fail("table of contents overrun");
    c.skip(toc_bytes - (c.pos() - toc_start));
    if (!c.ok()) return fail("table of contents overrun");

    const u8* data_start = c.raw(data_bytes);
    if (!data_start) return fail("missing data section");

    for (const auto& e : entries_) {
        if (e.offset + e.size > data_bytes) return fail("entry runs past the data section");
        payloads_[e.digest_hex()] = std::vector<u8>(data_start + e.offset, data_start + e.offset + e.size);
    }
    verified_ = verify_integrity();
    return true;
}

const PackEntry* PrismPack::find(std::string_view name) const {
    for (const auto& e : entries_) if (e.name == name) return &e;
    return nullptr;
}

const PackEntry* PrismPack::find(const Guid& g) const {
    for (const auto& e : entries_) if (e.guid == g) return &e;
    return nullptr;
}

std::vector<u8> PrismPack::payload(std::string_view name) const {
    const PackEntry* e = find(name);
    if (!e) return {};
    auto it = payloads_.find(e->digest_hex());
    return it == payloads_.end() ? std::vector<u8>{} : it->second;
}

std::string PrismPack::text(std::string_view name) const {
    const std::vector<u8> p = payload(name);
    return std::string(p.begin(), p.end());
}

bool PrismPack::verify_integrity() const {
    crypto::Sha256 manifest;
    for (const auto& e : entries_) {
        auto it = payloads_.find(e.digest_hex());
        if (it == payloads_.end()) return false;
        const crypto::Sha256Digest d = crypto::Sha256::hash(
            std::string_view(reinterpret_cast<const char*>(it->second.data()), it->second.size()));
        if (std::memcmp(d.data(), e.digest, 32) != 0) return false;
        manifest.update(d.data(), 32);
    }
    manifest_ = to_hex(manifest.finalize().data(), 32);
    return true;
}

u64 PrismPack::bytes_stored() const {
    u64 total = 0;
    for (const auto& kv : payloads_) total += kv.second.size();
    return total;
}

// ========================================================== asset database ==
bool AssetDatabase::upsert(std::string name, EntryKind kind, std::string_view content,
                           std::vector<std::string> deps) {
    const std::string digest = crypto::Sha256::hex(content);
    auto it = records_.find(name);
    if (it != records_.end() && it->second.digest_hex == digest && it->second.depends_on == deps) {
        return false;                                  // unchanged, nothing to rebuild
    }
    AssetRecord& r = records_[name];
    r.guid = Guid::deterministic(name);
    r.name = name;
    r.kind = kind;
    r.digest_hex = digest;
    r.size = content.size();
    r.dirty = true;
    r.depends_on = std::move(deps);
    return true;
}

void AssetDatabase::remove(const std::string& name) { records_.erase(name); }

const AssetRecord* AssetDatabase::get(std::string_view name) const {
    auto it = records_.find(std::string(name));
    return it == records_.end() ? nullptr : &it->second;
}

std::vector<std::string> AssetDatabase::names() const {
    std::vector<std::string> out;
    out.reserve(records_.size());
    for (const auto& kv : records_) out.push_back(kv.first);
    return out;
}

std::vector<std::string> AssetDatabase::dirty() const {
    std::vector<std::string> out;
    for (const auto& kv : records_) if (kv.second.dirty) out.push_back(kv.first);
    return out;
}

void AssetDatabase::clear_dirty() { for (auto& kv : records_) kv.second.dirty = false; }

std::vector<std::string> AssetDatabase::reverse_dependencies(std::string_view name) const {
    std::vector<std::string> out;
    std::vector<std::string> queue{std::string(name)};
    std::map<std::string, bool> seen;
    seen[std::string(name)] = true;
    while (!queue.empty()) {
        const std::string cur = queue.back();
        queue.pop_back();
        for (const auto& kv : records_) {
            if (seen.count(kv.first)) continue;
            for (const auto& d : kv.second.depends_on) {
                if (d == cur) {
                    seen[kv.first] = true;
                    out.push_back(kv.first);
                    queue.push_back(kv.first);
                    break;
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> AssetDatabase::build_order() const {
    // Kahn's algorithm over "dependency -> dependent" edges.
    std::map<std::string, i32> indegree;
    for (const auto& kv : records_) indegree[kv.first] = 0;
    for (const auto& kv : records_) {
        for (const auto& d : kv.second.depends_on) {
            if (records_.count(d)) indegree[kv.first]++;
        }
    }
    std::vector<std::string> queue;
    for (const auto& kv : indegree) if (kv.second == 0) queue.push_back(kv.first);
    std::sort(queue.begin(), queue.end());

    std::vector<std::string> order;
    while (!queue.empty()) {
        const std::string cur = queue.front();
        queue.erase(queue.begin());
        order.push_back(cur);
        for (const auto& kv : records_) {
            for (const auto& d : kv.second.depends_on) {
                if (d != cur) continue;
                if (--indegree[kv.first] == 0) queue.push_back(kv.first);
                break;
            }
        }
        std::sort(queue.begin(), queue.end());          // deterministic tie-break
    }
    if (order.size() != records_.size()) return {};     // cycle
    return order;
}

bool AssetDatabase::has_cycle() const { return build_order().empty() && !records_.empty(); }

std::string AssetDatabase::fingerprint() const {
    crypto::Sha256 h;
    for (const auto& kv : records_) {
        h.update(kv.first);
        h.update(kv.second.digest_hex);
        for (const auto& d : kv.second.depends_on) h.update(d);
    }
    return crypto::Sha256::to_hex(h.finalize());
}

u64 AssetDatabase::total_bytes() const {
    u64 t = 0;
    for (const auto& kv : records_) t += kv.second.size;
    return t;
}

// ================================================================= atlas ==
ShelfAtlas::ShelfAtlas(u32 width, u32 height, u32 padding)
    : width_(width ? width : 1), height_(height ? height : 1), padding_(padding) {}

AtlasRect ShelfAtlas::insert(u32 w, u32 h) {
    AtlasRect r;
    if (w == 0 || h == 0) return r;
    if (w + padding_ > width_ || h + padding_ > height_) return r;      // never fits
    if (cursor_x_ + w > width_) {                                       // start a new shelf
        cursor_y_ += shelf_h_;
        cursor_x_ = 0;
        shelf_h_ = 0;
    }
    if (cursor_y_ + h + padding_ > height_) return r;                   // out of space
    r.x = cursor_x_;
    r.y = cursor_y_;
    r.w = w;
    r.h = h;
    cursor_x_ += w + padding_;
    shelf_h_ = std::max(shelf_h_, h + padding_);
    used_area_ += w * h;
    ++count_;
    return r;
}

std::vector<AtlasRect> ShelfAtlas::insert_all(const std::vector<std::pair<u32, u32>>& sizes) {
    std::vector<AtlasRect> out;
    out.reserve(sizes.size());
    for (const auto& [w, h] : sizes) out.push_back(insert(w, h));
    return out;
}

void ShelfAtlas::reset() { cursor_x_ = 0; cursor_y_ = 0; shelf_h_ = 0; used_area_ = 0; count_ = 0; }

f32 ShelfAtlas::occupancy() const {
    const u32 used_h = cursor_y_ + shelf_h_;
    if (used_h == 0) return 0.0f;
    const u64 area = static_cast<u64>(width_) * used_h;
    return area == 0 ? 0.0f : static_cast<f32>(used_area_) / static_cast<f32>(area);
}

// ================================================================== mesh ==
void MeshData::compute_normals() {
    for (auto& v : vertices) v.normal = math::Vec3(0, 0, 0);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        MeshVertex& a = vertices[indices[i]];
        MeshVertex& b = vertices[indices[i + 1]];
        MeshVertex& c = vertices[indices[i + 2]];
        const math::Vec3 n = (b.position - a.position).cross(c.position - a.position);
        a.normal = a.normal + n;
        b.normal = b.normal + n;
        c.normal = c.normal + n;
    }
    for (auto& v : vertices) {
        const f32 len = v.normal.length();
        v.normal = len > 1e-6f ? v.normal / len : math::Vec3(0, 1, 0);
    }
}

void MeshData::compute_tangents() {
    std::vector<math::Vec3> tan(vertices.size(), math::Vec3(0, 0, 0));
    std::vector<math::Vec3> bitan(vertices.size(), math::Vec3(0, 0, 0));
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const MeshVertex& a = vertices[indices[i]];
        const MeshVertex& b = vertices[indices[i + 1]];
        const MeshVertex& c = vertices[indices[i + 2]];
        const math::Vec3 e1 = b.position - a.position;
        const math::Vec3 e2 = c.position - a.position;
        const f32 du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y;
        const f32 du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
        const f32 det = du1 * dv2 - du2 * dv1;
        const f32 r = std::fabs(det) > 1e-8f ? 1.0f / det : 0.0f;
        const math::Vec3 t((e1.x * dv2 - e2.x * dv1) * r, (e1.y * dv2 - e2.y * dv1) * r, (e1.z * dv2 - e2.z * dv1) * r);
        const math::Vec3 s((e2.x * du1 - e1.x * du2) * r, (e2.y * du1 - e1.y * du2) * r, (e2.z * du1 - e1.z * du2) * r);
        for (int k = 0; k < 3; ++k) {
            tan[indices[i + k]] = tan[indices[i + k]] + t;
            bitan[indices[i + k]] = bitan[indices[i + k]] + s;
        }
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const math::Vec3& n = vertices[i].normal;
        math::Vec3 t = tan[i] - n * n.dot(tan[i]);            // Gram-Schmidt
        const f32 len = t.length();
        t = len > 1e-6f ? t / len : math::Vec3(1, 0, 0);
        const f32 handedness = n.cross(t).dot(bitan[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = math::Vec4(t.x, t.y, t.z, handedness);
    }
}

void MeshData::compute_bounds() {
    if (vertices.empty()) { bounds_min = bounds_max = math::Vec3(); return; }
    bounds_min = bounds_max = vertices[0].position;
    for (const auto& v : vertices) {
        bounds_min.x = std::min(bounds_min.x, v.position.x);
        bounds_min.y = std::min(bounds_min.y, v.position.y);
        bounds_min.z = std::min(bounds_min.z, v.position.z);
        bounds_max.x = std::max(bounds_max.x, v.position.x);
        bounds_max.y = std::max(bounds_max.y, v.position.y);
        bounds_max.z = std::max(bounds_max.z, v.position.z);
    }
}

void MeshData::deduplicate_vertices() {
    std::map<std::string, u32> seen;
    std::vector<MeshVertex> out;
    for (u32& idx : indices) {
        if (idx >= vertices.size()) continue;
        const MeshVertex& v = vertices[idx];
        std::string key(reinterpret_cast<const char*>(&v), sizeof(MeshVertex));
        auto it = seen.find(key);
        if (it != seen.end()) { idx = it->second; continue; }
        seen[key] = static_cast<u32>(out.size());
        idx = static_cast<u32>(out.size());
        out.push_back(v);
    }
    vertices = std::move(out);
}

math::Vec3 MeshData::centre() const { return (bounds_min + bounds_max) * 0.5f; }

f32 MeshData::radius() const { return (bounds_max - bounds_min).length() * 0.5f; }

f32 MeshData::signed_volume() const {
    f64 vol = 0.0;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const math::Vec3& a = vertices[indices[i]].position;
        const math::Vec3& b = vertices[indices[i + 1]].position;
        const math::Vec3& c = vertices[indices[i + 2]].position;
        vol += static_cast<f64>(a.dot(b.cross(c))) / 6.0;
    }
    return static_cast<f32>(vol);
}

std::vector<u8> MeshData::to_bytes() const {
    std::vector<u8> out;
    put_magic(out, 0x4D455348u);                                // "MESH"
    put_u16(out, static_cast<u16>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    put_u32(out, static_cast<u32>(vertices.size()));
    put_u32(out, static_cast<u32>(indices.size()));
    for (const auto& v : vertices) {
        const f32 f[12] = {v.position.x, v.position.y, v.position.z,
                           v.normal.x, v.normal.y, v.normal.z,
                           v.uv.x, v.uv.y,
                           v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w};
        for (f32 x : f) { u32 bits; std::memcpy(&bits, &x, 4); put_u32(out, bits); }
    }
    for (u32 i : indices) put_u32(out, i);
    const f32 b[6] = {bounds_min.x, bounds_min.y, bounds_min.z, bounds_max.x, bounds_max.y, bounds_max.z};
    for (f32 x : b) { u32 bits; std::memcpy(&bits, &x, 4); put_u32(out, bits); }
    return out;
}

bool MeshData::from_bytes(const u8* data, std::size_t len, MeshData& out) {
    Cursor c(data, len);
    if (c.read_magic() != 0x4D455348u) return false;
    out = MeshData{};
    out.name = c.str(c.read_u16());
    const u32 nv = c.read_u32();
    const u32 ni = c.read_u32();
    if (nv > 4000000u || ni > 12000000u) return false;
    out.vertices.resize(nv);
    for (u32 i = 0; i < nv && c.ok(); ++i) {
        f32 f[12];
        for (f32& x : f) { u32 bits = c.read_u32(); std::memcpy(&x, &bits, 4); }
        out.vertices[i].position = math::Vec3(f[0], f[1], f[2]);
        out.vertices[i].normal = math::Vec3(f[3], f[4], f[5]);
        out.vertices[i].uv = math::Vec2(f[6], f[7]);
        out.vertices[i].tangent = math::Vec4(f[8], f[9], f[10], f[11]);
    }
    out.indices.resize(ni);
    for (u32 i = 0; i < ni && c.ok(); ++i) out.indices[i] = c.read_u32();
    f32 b[6];
    for (f32& x : b) { u32 bits = c.read_u32(); std::memcpy(&x, &bits, 4); }
    out.bounds_min = math::Vec3(b[0], b[1], b[2]);
    out.bounds_max = math::Vec3(b[3], b[4], b[5]);
    return c.ok();
}

// ------------------------------------------------------------ primitives --
namespace primitive {

namespace {
void push_face(MeshData& m, math::Vec3 n, math::Vec3 u, math::Vec3 v, f32 h) {
    const u32 base = static_cast<u32>(m.vertices.size());
    const math::Vec3 o = n * h;
    const math::Vec3 c[4] = {o - u * h - v * h, o + u * h - v * h, o + u * h + v * h, o - u * h + v * h};
    const math::Vec2 t[4] = {math::Vec2(0, 0), math::Vec2(1, 0), math::Vec2(1, 1), math::Vec2(0, 1)};
    for (int i = 0; i < 4; ++i) {
        MeshVertex vert;
        vert.position = c[i];
        vert.normal = n;
        vert.uv = t[i];
        vert.tangent = math::Vec4(u.x, u.y, u.z, 1.0f);
        m.vertices.push_back(vert);
    }
    m.indices.push_back(base + 0);
    m.indices.push_back(base + 1);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 0);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 3);
}
} // namespace

MeshData quad(f32 size) {
    MeshData m;
    m.name = "quad";
    push_face(m, math::Vec3(0, 0, 1), math::Vec3(1, 0, 0), math::Vec3(0, 1, 0), size * 0.5f);
    m.compute_bounds();
    return m;
}

MeshData cube(f32 size) {
    MeshData m;
    m.name = "cube";
    const f32 h = size * 0.5f;
    struct Face { math::Vec3 n, u, v; };
    const Face faces[6] = {
        {math::Vec3( 1, 0, 0), math::Vec3(0, 0, -1), math::Vec3(0, 1, 0)},
        {math::Vec3(-1, 0, 0), math::Vec3(0, 0,  1), math::Vec3(0, 1, 0)},
        {math::Vec3(0,  1, 0), math::Vec3(1, 0,  0), math::Vec3(0, 0, -1)},
        {math::Vec3(0, -1, 0), math::Vec3(1, 0,  0), math::Vec3(0, 0,  1)},
        {math::Vec3(0, 0,  1), math::Vec3(1, 0,  0), math::Vec3(0, 1, 0)},
        {math::Vec3(0, 0, -1), math::Vec3(-1, 0, 0), math::Vec3(0, 1, 0)},
    };
    for (const auto& f : faces) push_face(m, f.n, f.u, f.v, h);
    m.compute_bounds();
    return m;
}

MeshData uv_sphere(f32 radius, u32 segments, u32 rings) {
    MeshData m;
    m.name = "sphere";
    segments = segments < 3 ? 3 : segments;
    rings = rings < 2 ? 2 : rings;
    for (u32 y = 0; y <= rings; ++y) {
        const f32 v = static_cast<f32>(y) / static_cast<f32>(rings);
        const f32 phi = v * math::Pi;
        for (u32 x = 0; x <= segments; ++x) {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(segments);
            const f32 theta = u * math::TwoPi;
            MeshVertex vert;
            const f32 sy = std::sin(phi);
            vert.position = math::Vec3(sy * std::cos(theta), std::cos(phi), sy * std::sin(theta)) * radius;
            vert.normal = vert.position.normalized();
            vert.uv = math::Vec2(u, v);
            m.vertices.push_back(vert);
        }
    }
    const u32 stride = segments + 1;
    // phi runs from the +Y pole downwards, so the ring index increases with
    // -Y; the winding below accounts for that or the sphere culls inside-out.
    for (u32 y = 0; y < rings; ++y) {
        for (u32 x = 0; x < segments; ++x) {
            const u32 a = y * stride + x;
            const u32 b = a + stride;
            m.indices.insert(m.indices.end(), {a, a + 1, b});
            m.indices.insert(m.indices.end(), {a + 1, b + 1, b});
        }
    }
    m.compute_tangents();
    m.compute_bounds();
    return m;
}

MeshData cylinder(f32 radius, f32 height, u32 segments) {
    MeshData m;
    m.name = "cylinder";
    segments = segments < 3 ? 3 : segments;
    const f32 half = height * 0.5f;
    for (u32 ring = 0; ring < 2; ++ring) {
        const f32 y = ring ? half : -half;
        for (u32 i = 0; i <= segments; ++i) {
            const f32 a = static_cast<f32>(i) / static_cast<f32>(segments) * math::TwoPi;
            MeshVertex vert;
            vert.position = math::Vec3(std::cos(a) * radius, y, std::sin(a) * radius);
            vert.normal = math::Vec3(std::cos(a), 0, std::sin(a));
            vert.uv = math::Vec2(static_cast<f32>(i) / static_cast<f32>(segments), static_cast<f32>(ring));
            m.vertices.push_back(vert);
        }
    }
    const u32 stride = segments + 1;
    for (u32 i = 0; i < segments; ++i) {
        const u32 a = i, b = i + stride;
        m.indices.insert(m.indices.end(), {a, b, a + 1});
        m.indices.insert(m.indices.end(), {a + 1, b, b + 1});
    }
    m.compute_bounds();
    return m;
}

MeshData grid(u32 cells, f32 cell_size) {
    MeshData m;
    m.name = "grid";
    cells = cells < 1 ? 1 : cells;
    const f32 half = static_cast<f32>(cells) * cell_size * 0.5f;
    for (u32 z = 0; z <= cells; ++z) {
        for (u32 x = 0; x <= cells; ++x) {
            MeshVertex vert;
            vert.position = math::Vec3(static_cast<f32>(x) * cell_size - half, 0, static_cast<f32>(z) * cell_size - half);
            vert.normal = math::Vec3(0, 1, 0);
            vert.uv = math::Vec2(static_cast<f32>(x) / cells, static_cast<f32>(z) / cells);
            m.vertices.push_back(vert);
        }
    }
    const u32 stride = cells + 1;
    for (u32 z = 0; z < cells; ++z) {
        for (u32 x = 0; x < cells; ++x) {
            const u32 a = z * stride + x;
            const u32 b = a + stride;
            m.indices.insert(m.indices.end(), {a, b, a + 1});
            m.indices.insert(m.indices.end(), {a + 1, b, b + 1});
        }
    }
    m.compute_tangents();
    m.compute_bounds();
    return m;
}
} // namespace primitive

// ========================================================== pixel formats ==
const char* pixel_format_name(PixelFormat f) {
    switch (f) {
        case PixelFormat::R8: return "r8";
        case PixelFormat::RG8: return "rg8";
        case PixelFormat::RGB8: return "rgb8";
        case PixelFormat::RGBA8: return "rgba8";
        case PixelFormat::R16F: return "r16f";
        case PixelFormat::RGBA16F: return "rgba16f";
        case PixelFormat::R32F: return "r32f";
        case PixelFormat::RGBA32F: return "rgba32f";
        case PixelFormat::ETC2_RGB8: return "etc2_rgb8";
        case PixelFormat::ASTC_4x4: return "astc_4x4";
    }
    return "unknown";
}

u32 bits_per_pixel(PixelFormat f) {
    switch (f) {
        case PixelFormat::R8: return 8;
        case PixelFormat::RG8: return 16;
        case PixelFormat::RGB8: return 24;
        case PixelFormat::RGBA8: return 32;
        case PixelFormat::R16F: return 16;
        case PixelFormat::RGBA16F: return 64;
        case PixelFormat::R32F: return 32;
        case PixelFormat::RGBA32F: return 128;
        case PixelFormat::ETC2_RGB8: return 4;        // 64 bits per 4x4 block
        case PixelFormat::ASTC_4x4: return 8;         // 128 bits per 4x4 block
    }
    return 0;
}

std::size_t image_bytes(PixelFormat f, u32 w, u32 h) {
    if (f == PixelFormat::ETC2_RGB8 || f == PixelFormat::ASTC_4x4) {
        const u32 bw = (w + 3) / 4, bh = (h + 3) / 4;
        return static_cast<std::size_t>(bw) * bh * (f == PixelFormat::ASTC_4x4 ? 16 : 8);
    }
    return static_cast<std::size_t>(w) * h * bits_per_pixel(f) / 8;
}

u32 Texture::mip_levels() const {
    u32 levels = 1;
    u32 w = width, h = height;
    while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++levels; }
    return levels;
}

void Texture::build_mip_chain() {
    mips.clear();
    if (format != PixelFormat::RGBA8 || width == 0 || height == 0) return;
    u32 w = width, h = height;
    std::vector<u8> cur = pixels;
    while (w > 1 || h > 1) {
        u32 nw = 0, nh = 0;
        // Separate output vector: moving into `cur` and then reading it on the
        // next pass would downsample a null buffer and stop one level early.
        std::vector<u8> next = box_downsample(cur.data(), w, h, nw, nh);
        if (nw == 0 || nh == 0 || next.empty()) break;
        w = nw;
        h = nh;
        cur = std::move(next);
        mips.push_back(cur);
    }
}

math::Color Texture::sample_nearest(f32 u, f32 v) const {
    if (format != PixelFormat::RGBA8 || width == 0 || height == 0 || pixels.size() < static_cast<std::size_t>(width) * height * 4)
        return math::Color{0.0f, 0.0f, 0.0f, 1.0f};
    u = u - std::floor(u);
    v = v - std::floor(v);
    const u32 x = static_cast<u32>(u * width) % width;
    const u32 y = static_cast<u32>(v * height) % height;
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
    return math::Color{pixels[i] / 255.0f, pixels[i + 1] / 255.0f, pixels[i + 2] / 255.0f, pixels[i + 3] / 255.0f};
}

f32 Texture::average_luminance() const {
    if (format != PixelFormat::RGBA8 || pixels.size() < 4) return 0.0f;
    f64 sum = 0;
    const std::size_t n = pixels.size() / 4;
    for (std::size_t i = 0; i < n; ++i) {
        const u8* p = &pixels[i * 4];
        sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    }
    return static_cast<f32>(sum / static_cast<f64>(n) / 255.0);
}

std::vector<u8> box_downsample(const u8* src, u32 w, u32 h, u32& out_w, u32& out_h) {
    out_w = out_h = 0;
    if (!src || w == 0 || h == 0) return {};
    out_w = std::max(1u, w / 2);
    out_h = std::max(1u, h / 2);
    std::vector<u8> dst(static_cast<std::size_t>(out_w) * out_h * 4, 0);
    for (u32 y = 0; y < out_h; ++y) {
        const u32 sy = std::min(y * 2, h - 1);
        const u32 sy2 = std::min(sy + 1, h - 1);
        for (u32 x = 0; x < out_w; ++x) {
            const u32 sx = std::min(x * 2, w - 1);
            const u32 sx2 = std::min(sx + 1, w - 1);
            const u8* a = &src[(static_cast<std::size_t>(sy) * w + sx) * 4];
            const u8* b = &src[(static_cast<std::size_t>(sy) * w + sx2) * 4];
            const u8* c = &src[(static_cast<std::size_t>(sy2) * w + sx) * 4];
            const u8* d = &src[(static_cast<std::size_t>(sy2) * w + sx2) * 4];
            u8* o = &dst[(static_cast<std::size_t>(y) * out_w + x) * 4];
            for (int k = 0; k < 4; ++k) o[k] = static_cast<u8>((a[k] + b[k] + c[k] + d[k]) / 4);
        }
    }
    return dst;
}

// ============================================================== ETC1/ETC2 ==
namespace etc1 {

const i32 kModifierTable[8][2] = {
    {2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 182}
};

namespace {
/// Modifier for a 2-bit pixel index: [-large, -small, +small, +large].
inline i32 modifier(i32 table, i32 index) {
    const i32 small = kModifierTable[table][0];
    const i32 large = kModifierTable[table][1];
    switch (index) {
        case 0: return -large;
        case 1: return -small;
        case 2: return small;
        default: return large;
    }
}

inline u8 clamp8(i32 v) { return static_cast<u8>(clampi(v, 0, 255)); }

inline i32 sign_extend3(i32 v) { return (v & 4) ? (v | ~3) : v; }
} // namespace

void decode_block(const u8 block[8], u8 out_rgba[64]) {
    const i32 table_a = (block[3] >> 5) & 7;
    const i32 table_b = (block[3] >> 2) & 7;
    const bool diff = (block[3] & 2) != 0;
    const bool flip = (block[3] & 1) != 0;

    i32 base[2][3];
    if (diff) {
        // 5-bit base + 3-bit signed delta per channel.
        const i32 r = ((block[0] >> 3) & 0x1F);
        const i32 g = ((block[1] >> 3) & 0x1F);
        const i32 b = ((block[2] >> 3) & 0x1F);
        const i32 dr = sign_extend3(block[0] & 7);
        const i32 dg = sign_extend3(block[1] & 7);
        const i32 db = sign_extend3(block[2] & 7);
        base[0][0] = (r << 3) | (r >> 2);
        base[0][1] = (g << 3) | (g >> 2);
        base[0][2] = (b << 3) | (b >> 2);
        // The sub-block 2 base wraps in 5-bit space before the 5->8 expansion.
        const i32 r2 = (r + dr) & 0x1F, g2 = (g + dg) & 0x1F, b2 = (b + db) & 0x1F;
        base[1][0] = (r2 << 3) | (r2 >> 2);
        base[1][1] = (g2 << 3) | (g2 >> 2);
        base[1][2] = (b2 << 3) | (b2 >> 2);
    } else {
        for (int s = 0; s < 2; ++s) {
            const i32 shift = s ? 0 : 4;
            const i32 r4 = (block[0] >> shift) & 0xF;
            const i32 g4 = (block[1] >> shift) & 0xF;
            const i32 b4 = (block[2] >> shift) & 0xF;
            base[s][0] = (r4 << 4) | r4;
            base[s][1] = (g4 << 4) | g4;
            base[s][2] = (b4 << 4) | b4;
        }
    }

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            // flip=0 splits by column, flip=1 splits by row.
            const i32 sub = flip ? (y >= 2 ? 1 : 0) : (x >= 2 ? 1 : 0);
            const i32 bit = x * 4 + y;                       // ETC column-major bit order
            const i32 msb = (block[4 + (bit >> 3)] >> (bit & 7)) & 1;
            const i32 lsb = (block[6 + (bit >> 3)] >> (bit & 7)) & 1;
            const i32 mod = modifier(sub ? table_b : table_a, (msb << 1) | lsb);
            u8* o = &out_rgba[(static_cast<std::size_t>(y) * 4 + x) * 4];
            o[0] = clamp8(base[sub][0] + mod);
            o[1] = clamp8(base[sub][1] + mod);
            o[2] = clamp8(base[sub][2] + mod);
            o[3] = 255;
        }
    }
}

void encode_block(const u8* rgba, u8 out[8]) {
    std::memset(out, 0, 8);
    u8 bases[2][3];
    i32 tables[2] = {0, 0};
    u8 indices[16];

    for (int sub = 0; sub < 2; ++sub) {
        // Candidate 4-bit base colours: floor and ceil of the channel mean.
        i32 cand[3][2];
        for (int c = 0; c < 3; ++c) {
            i32 sum = 0;
            for (int y = sub * 2; y < sub * 2 + 2; ++y)
                for (int x = 0; x < 4; ++x) sum += rgba[(y * 4 + x) * 4 + c];
            const f32 mean = static_cast<f32>(sum) / 8.0f / 17.0f;
            const i32 lo = clampi(static_cast<i32>(std::floor(mean)), 0, 15);
            cand[c][0] = lo;
            cand[c][1] = clampi(lo + 1, 0, 15);
        }

        i64 best_err = INT64_MAX;
        i32 best[3] = {0, 0, 0};
        i32 best_table = 0;
        for (int t = 0; t < 8; ++t) {
            for (int ri = 0; ri < 2; ++ri) {
                for (int gi = 0; gi < 2; ++gi) {
                    for (int bi = 0; bi < 2; ++bi) {
                        const i32 base8[3] = {cand[0][ri] * 17, cand[1][gi] * 17, cand[2][bi] * 17};
                        i64 err = 0;
                        for (int y = sub * 2; y < sub * 2 + 2; ++y) {
                            for (int x = 0; x < 4; ++x) {
                                const u8* p = &rgba[(y * 4 + x) * 4];
                                i32 local = INT32_MAX;
                                for (int idx = 0; idx < 4; ++idx) {
                                    const i32 m = modifier(t, idx);
                                    i32 d = 0;
                                    for (int c = 0; c < 3; ++c) {
                                        const i32 e = clamp8(base8[c] + m) - p[c];
                                        d += e * e;
                                    }
                                    local = std::min(local, d);
                                }
                                err += local;
                            }
                        }
                        if (err < best_err) {
                            best_err = err;
                            best_table = t;
                            best[0] = cand[0][ri];
                            best[1] = cand[1][gi];
                            best[2] = cand[2][bi];
                        }
                    }
                }
            }
        }
        bases[sub][0] = static_cast<u8>(best[0]);
        bases[sub][1] = static_cast<u8>(best[1]);
        bases[sub][2] = static_cast<u8>(best[2]);
        tables[sub] = best_table;

        // Fix the pixel indices for the winning configuration.
        const i32 base8[3] = {best[0] * 17, best[1] * 17, best[2] * 17};
        for (int y = sub * 2; y < sub * 2 + 2; ++y) {
            for (int x = 0; x < 4; ++x) {
                const u8* p = &rgba[(y * 4 + x) * 4];
                i32 best_idx = 0, best_d = INT32_MAX;
                for (int idx = 0; idx < 4; ++idx) {
                    const i32 m = modifier(best_table, idx);
                    i32 d = 0;
                    for (int c = 0; c < 3; ++c) { const i32 e = clamp8(base8[c] + m) - p[c]; d += e * e; }
                    if (d < best_d) { best_d = d; best_idx = idx; }
                }
                indices[y * 4 + x] = static_cast<u8>(best_idx);
            }
        }
    }

    out[0] = static_cast<u8>((bases[0][0] << 4) | bases[1][0]);
    out[1] = static_cast<u8>((bases[0][1] << 4) | bases[1][1]);
    out[2] = static_cast<u8>((bases[0][2] << 4) | bases[1][2]);
    out[3] = static_cast<u8>((tables[0] << 5) | (tables[1] << 2) | 0x01);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const i32 bit = x * 4 + y;
            const i32 idx = indices[y * 4 + x];
            const i32 msb = (idx >> 1) & 1;
            const i32 lsb = idx & 1;
            out[4 + (bit >> 3)] |= static_cast<u8>(msb << (bit & 7));
            out[6 + (bit >> 3)] |= static_cast<u8>(lsb << (bit & 7));
        }
    }
}

std::size_t encoded_size(u32 w, u32 h) {
    return static_cast<std::size_t>((w + 3) / 4) * ((h + 3) / 4) * 8;
}

std::vector<u8> encode(const u8* rgba, u32 w, u32 h) {
    std::vector<u8> out(encoded_size(w, h), 0);
    if (!rgba) return out;
    const u32 bw = w / 4, bh = h / 4;
    for (u32 by = 0; by < bh; ++by) {
        for (u32 bx = 0; bx < bw; ++bx) {
            u8 block_src[64];
            for (u32 y = 0; y < 4; ++y)
                for (u32 x = 0; x < 4; ++x)
                    std::memcpy(&block_src[(y * 4 + x) * 4], &rgba[(((by * 4 + y) * w) + bx * 4 + x) * 4], 4);
            encode_block(block_src, &out[(static_cast<std::size_t>(by) * bw + bx) * 8]);
        }
    }
    return out;
}

std::vector<u8> decode(const u8* etc, u32 w, u32 h) {
    std::vector<u8> out(static_cast<std::size_t>(w) * h * 4, 0);
    if (!etc) return out;
    const u32 bw = w / 4, bh = h / 4;
    for (u32 by = 0; by < bh; ++by) {
        for (u32 bx = 0; bx < bw; ++bx) {
            u8 rgba[64];
            decode_block(&etc[(static_cast<std::size_t>(by) * bw + bx) * 8], rgba);
            for (u32 y = 0; y < 4; ++y)
                for (u32 x = 0; x < 4; ++x)
                    std::memcpy(&out[(((by * 4 + y) * w) + bx * 4 + x) * 4], &rgba[(y * 4 + x) * 4], 4);
        }
    }
    return out;
}

f32 round_trip_error(const u8* rgba, u32 w, u32 h) {
    if (!rgba || w == 0 || h == 0) return 0.0f;
    const std::vector<u8> etc = encode(rgba, w, h);
    const std::vector<u8> back = decode(etc.data(), w, h);
    f64 sum = 0;
    const std::size_t n = static_cast<std::size_t>(w) * h * 3;
    for (std::size_t i = 0; i < n; ++i) sum += std::fabs(static_cast<i32>(back[i]) - static_cast<i32>(rgba[i]));
    return static_cast<f32>(sum / static_cast<f64>(n));
}

} // namespace etc1

} // namespace prism::assets
