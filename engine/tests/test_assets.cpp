// =====================================================================
//  PRISM ENGINE — tests/test_assets.cpp
//  The .prism container, import database, atlas packer, meshes and the
//  ETC1/ETC2 block codec (known-answer vectors for the decoder).
// =====================================================================
#include "prism_test.h"
#include "prism/assets/assets.h"

#include <cstring>

using namespace prism;
using namespace prism::assets;

namespace {
std::vector<u8> bytes_of(std::string_view s) { return std::vector<u8>(s.begin(), s.end()); }

std::vector<u8> solid_rgba(u32 w, u32 h, u8 r, u8 g, u8 b) {
    std::vector<u8> img(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        img[i * 4 + 0] = r; img[i * 4 + 1] = g; img[i * 4 + 2] = b; img[i * 4 + 3] = 255;
    }
    return img;
}
} // namespace

// ==================================================================== guid ==
PRISM_TEST(assets_guid_is_deterministic_and_roundtrips) {
    const Guid a = Guid::deterministic("assets/hero.mesh");
    const Guid b = Guid::deterministic("assets/hero.mesh");
    const Guid c = Guid::deterministic("assets/villain.mesh");
    PRISM_CHECK(a == b);
    PRISM_CHECK(!(a == c));
    PRISM_CHECK(!a.null());
    PRISM_CHECK(Guid().null());

    const std::string text = a.to_string();
    PRISM_CHECK_EQ(text.size(), 33u);                // 16 + '-' + 16
    PRISM_CHECK(Guid::from_string(text) == a);
    // Renaming the file changes the guid; that is what the editor's
    // rename-stable remap table is for, and it must not collide by accident.
    PRISM_CHECK(!(Guid::deterministic("assets/hero.mesh ") == a));
}

// ============================================================ .prism pack ==
PRISM_TEST(assets_pack_roundtrip) {
    PrismPack pack;
    pack.add_text("scenes/main.scene.json", EntryKind::Scene, R"({"name":"main"})");
    pack.add_text("scripts/player.prism", EntryKind::Script, "func update() {}");
    pack.add("meshes/cube.mesh", EntryKind::Mesh, bytes_of("\x01\x02\x03\x04"),
             {Guid::deterministic("materials/std.material")});

    const std::vector<u8> blob = pack.build();
    PRISM_CHECK(blob.size() > 20u);   // 20-byte header + TOC + payload
    PRISM_CHECK_EQ(blob[0], 'P');
    PRISM_CHECK_EQ(blob[1], 'R');
    PRISM_CHECK_EQ(blob[2], 'I');
    PRISM_CHECK_EQ(blob[3], 'S');

    PrismPack loaded;
    std::string err;
    PRISM_CHECK(loaded.load(blob.data(), blob.size(), &err));
    PRISM_CHECK(loaded.verified());
    PRISM_CHECK_EQ(loaded.entry_count(), 3u);
    PRISM_CHECK_STR(loaded.text("scenes/main.scene.json"), R"({"name":"main"})");
    PRISM_CHECK_STR(loaded.text("scripts/player.prism"), "func update() {}");
    PRISM_CHECK_EQ(loaded.payload("meshes/cube.mesh").size(), 4u);


    const PackEntry* mesh = loaded.find("meshes/cube.mesh");
    PRISM_CHECK(mesh != nullptr);
    PRISM_CHECK(mesh->kind == EntryKind::Mesh);
    PRISM_CHECK_STR(entry_kind_name(mesh->kind), "mesh");
    PRISM_CHECK_EQ(mesh->dependencies.size(), 1u);
    PRISM_CHECK(mesh->dependencies[0] == Guid::deterministic("materials/std.material"));
    PRISM_CHECK(loaded.find(mesh->guid) == mesh);
    PRISM_CHECK(loaded.find("does/not/exist") == nullptr);
    PRISM_CHECK_EQ(loaded.manifest_hex().size(), 64u);
    // The manifest is a pure function of the entry digests, so both sides agree.
    PRISM_CHECK(pack.verify_integrity());
    PRISM_CHECK_STR(loaded.manifest_hex(), pack.manifest_hex());
    PRISM_CHECK_EQ(loaded.bytes_stored(), pack.bytes_stored());
}

PRISM_TEST(assets_pack_deduplicates_identical_content) {
    PrismPack pack;
    const std::string shared = "a payload shared by three entries";
    pack.add_text("a.bin", EntryKind::Binary, shared);
    pack.add_text("b.bin", EntryKind::Binary, shared);
    pack.add_text("c.bin", EntryKind::Binary, shared);
    pack.add_text("d.bin", EntryKind::Binary, "unique");

    PRISM_CHECK_EQ(pack.bytes_stored(), shared.size() + 6u);
    PRISM_CHECK_EQ(pack.bytes_deduplicated(), shared.size() * 2);

    const std::vector<u8> blob = pack.build();
    PrismPack loaded;
    PRISM_CHECK(loaded.load(blob.data(), blob.size()));
    PRISM_CHECK_EQ(loaded.entry_count(), 4u);
    // Every logical name still resolves to the same bytes.
    PRISM_CHECK_STR(loaded.text("a.bin"), shared);
    PRISM_CHECK_STR(loaded.text("c.bin"), shared);
    PRISM_CHECK_EQ(loaded.bytes_stored(), shared.size() + 6u);
}

PRISM_TEST(assets_pack_detects_tampering) {
    PrismPack pack;
    pack.add_text("data/config.json", EntryKind::Json, R"({"difficulty":"chill"})");
    std::vector<u8> blob = pack.build();

    // Corrupt one byte of the payload (past the 20-byte header and the TOC).
    blob[blob.size() - 1] ^= 0x40;
    PrismPack tampered;
    PRISM_CHECK(tampered.load(blob.data(), blob.size()));
    PRISM_CHECK(!tampered.verified());
    PRISM_CHECK(!tampered.verify_integrity());
}

PRISM_TEST(assets_pack_rejects_malformed_input) {
    PrismPack pack;
    pack.add_text("x.txt", EntryKind::Text, "hello");
    std::vector<u8> blob = pack.build();

    PrismPack bad;
    std::string err;
    std::vector<u8> not_a_pack = blob;
    not_a_pack[0] = 'X';
    PRISM_CHECK(!bad.load(not_a_pack.data(), not_a_pack.size(), &err));
    PRISM_CHECK(!err.empty());

    PRISM_CHECK(!bad.load(blob.data(), 10, &err));          // truncated header
    PRISM_CHECK(!bad.load(blob.data(), blob.size() - 1, &err));   // truncated payload
    PRISM_CHECK(!bad.load(nullptr, 0, &err));
}

// ========================================================== asset database ==
PRISM_TEST(assets_database_tracks_dirty_and_dedupes_reimports) {
    AssetDatabase db;
    PRISM_CHECK(db.upsert("hero.mesh", EntryKind::Mesh, "v1"));
    PRISM_CHECK(!db.upsert("hero.mesh", EntryKind::Mesh, "v1"));   // unchanged
    PRISM_CHECK(db.upsert("hero.mesh", EntryKind::Mesh, "v2"));    // changed
    PRISM_CHECK_EQ(db.size(), 1u);
    PRISM_CHECK(db.dirty().size() == 1u);
    db.clear_dirty();
    PRISM_CHECK(db.dirty().empty());

    const AssetRecord* r = db.get("hero.mesh");
    PRISM_CHECK(r != nullptr);
    PRISM_CHECK_EQ(r->size, 2u);
    PRISM_CHECK(r->kind == EntryKind::Mesh);
    PRISM_CHECK(!r->digest_hex.empty());
    PRISM_CHECK(r->guid == Guid::deterministic("hero.mesh"));
    PRISM_CHECK(db.get("missing") == nullptr);
    PRISM_CHECK_EQ(db.total_bytes(), 2u);

    const std::string before = db.fingerprint();
    db.upsert("hero.mesh", EntryKind::Mesh, "v3");
    PRISM_CHECK(!(db.fingerprint() == before));
    db.remove("hero.mesh");
    PRISM_CHECK_EQ(db.size(), 0u);
}

PRISM_TEST(assets_database_dependency_graph_and_build_order) {
    AssetDatabase db;
    db.upsert("shader.pbr.shader", EntryKind::Shader, "s");
    db.upsert("hero.material", EntryKind::Material, "m", {"shader.pbr.shader"});
    db.upsert("hero.prefab", EntryKind::Prefab, "p", {"hero.material"});
    db.upsert("level.scene", EntryKind::Scene, "l", {"hero.prefab"});

    const auto order = db.build_order();
    PRISM_CHECK_EQ(order.size(), 4u);
    // Dependencies must come before their dependents.
    auto pos = [&](const std::string& n) {
        for (std::size_t i = 0; i < order.size(); ++i) if (order[i] == n) return static_cast<int>(i);
        return -1;
    };
    PRISM_CHECK(pos("shader.pbr.shader") < pos("hero.material"));
    PRISM_CHECK(pos("hero.material") < pos("hero.prefab"));
    PRISM_CHECK(pos("hero.prefab") < pos("level.scene"));
    PRISM_CHECK(!db.has_cycle());

    const auto rev = db.reverse_dependencies("shader.pbr.shader");
    PRISM_CHECK_EQ(rev.size(), 3u);
    PRISM_CHECK(rev[0] == "hero.material");
    PRISM_CHECK(rev[1] == "hero.prefab");
    PRISM_CHECK(rev[2] == "level.scene");
    PRISM_CHECK(db.reverse_dependencies("level.scene").empty());

    // An unknown dependency is ignored rather than creating a phantom node.
    db.upsert("orphan.mat", EntryKind::Material, "o", {"not/imported.shader"});
    PRISM_CHECK_EQ(db.build_order().size(), 5u);
}

PRISM_TEST(assets_database_detects_a_dependency_cycle) {
    AssetDatabase db;
    db.upsert("a.mat", EntryKind::Material, "a", {"b.prefab"});
    db.upsert("b.prefab", EntryKind::Prefab, "b", {"a.mat"});
    PRISM_CHECK(db.build_order().empty());
    PRISM_CHECK(db.has_cycle());
}

// ================================================================== atlas ==
PRISM_TEST(assets_atlas_shelf_packing_is_deterministic_and_padded) {
    ShelfAtlas atlas(128, 128, 1);
    PRISM_CHECK_EQ(atlas.width(), 128u);

    // 32-wide sprites take 33 columns with padding -> three per 128-px row,
    // and three 33-px rows fit in 128 px, so exactly nine rects fit.
    std::vector<std::pair<u32, u32>> sizes(12, {32, 32});
    const auto rects = atlas.insert_all(sizes);
    PRISM_CHECK_EQ(rects.size(), 12u);
    int valid = 0;
    for (const auto& r : rects) if (r.valid()) ++valid;
    PRISM_CHECK_EQ(valid, 9);
    PRISM_CHECK_EQ(atlas.insert_count(), 9u);

    PRISM_CHECK_EQ(rects[0].x, 0u);  PRISM_CHECK_EQ(rects[0].y, 0u);
    PRISM_CHECK_EQ(rects[1].x, 33u); PRISM_CHECK_EQ(rects[1].y, 0u);
    PRISM_CHECK_EQ(rects[2].x, 66u); PRISM_CHECK_EQ(rects[2].y, 0u);
    PRISM_CHECK_EQ(rects[3].x, 0u);  PRISM_CHECK_EQ(rects[3].y, 33u);
    PRISM_CHECK(!rects[9].valid());

    // No two rects may overlap.
    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            if (!rects[i].valid() || !rects[j].valid()) continue;
            const bool overlap = rects[i].x < rects[j].x + rects[j].w && rects[j].x < rects[i].x + rects[i].w &&
                                 rects[i].y < rects[j].y + rects[j].h && rects[j].y < rects[i].y + rects[i].h;
            PRISM_CHECK(!overlap);
        }
    }
    PRISM_CHECK_EQ(atlas.used_height(), 99u);
    PRISM_CHECK(atlas.occupancy() > 0.7f);
    PRISM_CHECK(atlas.occupancy() <= 1.0f);

    // Oversized and degenerate requests are refused, not wrapped.
    PRISM_CHECK(!atlas.insert(200, 10).valid());
    PRISM_CHECK(!atlas.insert(0, 5).valid());
    PRISM_CHECK(!atlas.insert(5, 0).valid());

    atlas.reset();
    PRISM_CHECK_EQ(atlas.insert_count(), 0u);
    PRISM_CHECK(atlas.insert(64, 64).valid());
}

// =================================================================== mesh ==
PRISM_TEST(assets_mesh_cube_is_closed_and_outward_facing) {
    const MeshData cube = primitive::cube(2.0f);
    PRISM_CHECK_EQ(cube.vertices.size(), 24u);
    PRISM_CHECK_EQ(cube.indices.size(), 36u);
    PRISM_CHECK_EQ(cube.triangle_count(), 12u);
    PRISM_CHECK_NEAR(cube.bounds_min.x, -1.0f, 1e-5f);
    PRISM_CHECK_NEAR(cube.bounds_max.y, 1.0f, 1e-5f);
    PRISM_CHECK_NEAR(cube.centre().x, 0.0f, 1e-5f);
    PRISM_CHECK_NEAR(cube.radius(), std::sqrt(3.0f), 1e-4f);

    // Positive signed volume means the winding faces outward.
    PRISM_CHECK_NEAR(cube.signed_volume(), 8.0f, 1e-3f);
    // Unit normals on every vertex.
    for (const auto& v : cube.vertices) PRISM_CHECK_NEAR(v.normal.length(), 1.0f, 1e-4f);
    // Every index in range.
    for (u32 i : cube.indices) PRISM_CHECK(i < cube.vertices.size());
}

PRISM_TEST(assets_mesh_primitives_are_well_formed) {
    const MeshData q = primitive::quad(1.0f);
    PRISM_CHECK_EQ(q.vertices.size(), 4u);
    PRISM_CHECK_EQ(q.triangle_count(), 2u);
    PRISM_CHECK_NEAR(q.bounds_max.z, 0.5f, 1e-5f);

    const MeshData sphere = primitive::uv_sphere(1.0f, 16, 8);
    PRISM_CHECK_EQ(sphere.vertices.size(), 9u * 17u);
    PRISM_CHECK_EQ(sphere.triangle_count(), 8u * 16u * 2u);
    // Positive, and close to 4.19: a negative volume means the winding is
    // inside-out and the sphere would be back-face culled away.
    PRISM_CHECK(sphere.signed_volume() > 3.8f);
    PRISM_CHECK(sphere.signed_volume() < 4.2f);
    for (const auto& v : sphere.vertices) PRISM_CHECK_NEAR(v.normal.length(), 1.0f, 1e-3f);

    const MeshData cyl = primitive::cylinder(1.0f, 2.0f, 12);
    PRISM_CHECK_EQ(cyl.vertices.size(), 2u * 13u);
    PRISM_CHECK_NEAR(cyl.bounds_max.y, 1.0f, 1e-5f);
    PRISM_CHECK_NEAR(cyl.bounds_min.y, -1.0f, 1e-5f);

    const MeshData grid = primitive::grid(4, 1.0f);
    PRISM_CHECK_EQ(grid.vertices.size(), 25u);
    PRISM_CHECK_EQ(grid.triangle_count(), 32u);
    PRISM_CHECK_NEAR(grid.bounds_max.x, 2.0f, 1e-5f);
    PRISM_CHECK(grid.signed_volume() == 0.0f);             // planar
}

PRISM_TEST(assets_mesh_tangents_are_orthonormal) {
    MeshData q = primitive::quad(1.0f);
    q.compute_tangents();
    for (const auto& v : q.vertices) {
        const math::Vec3 t(v.tangent.x, v.tangent.y, v.tangent.z);
        PRISM_CHECK_NEAR(t.length(), 1.0f, 1e-4f);
        PRISM_CHECK_NEAR(t.dot(v.normal), 0.0f, 1e-4f);    // Gram-Schmidt orthogonal
        PRISM_CHECK(std::fabs(v.tangent.w) == 1.0f);
        PRISM_CHECK_NEAR(v.tangent.x, 1.0f, 1e-4f);        // +U runs along +X
    }
}

PRISM_TEST(assets_mesh_deduplicates_shared_vertices) {
    MeshData m;
    m.name = "split-quad";
    // Two triangles that share an edge, stored with duplicated corners.
    const math::Vec3 p[6] = {math::Vec3(-1, -1, 0), math::Vec3(1, -1, 0), math::Vec3(1, 1, 0),
                             math::Vec3(-1, -1, 0), math::Vec3(1, 1, 0), math::Vec3(-1, 1, 0)};
    m.vertices.resize(6);
    for (int i = 0; i < 6; ++i) {
        m.vertices[i].position = p[i];
        m.vertices[i].normal = math::Vec3(0, 0, 1);
        m.vertices[i].uv = math::Vec2(p[i].x, p[i].y);
        m.vertices[i].tangent = math::Vec4(1, 0, 0, 1);
    }
    m.indices = {0, 1, 2, 3, 4, 5};
    m.compute_bounds();
    PRISM_CHECK_EQ(m.vertices.size(), 6u);
    m.deduplicate_vertices();
    PRISM_CHECK_EQ(m.vertices.size(), 4u);
    PRISM_CHECK_EQ(m.indices.size(), 6u);
    PRISM_CHECK_EQ(m.triangle_count(), 2u);
    for (u32 i : m.indices) PRISM_CHECK(i < 4u);
}

PRISM_TEST(assets_mesh_serialisation_roundtrip) {
    MeshData src = primitive::uv_sphere(1.5f, 12, 6);
    src.compute_tangents();
    const std::vector<u8> blob = src.to_bytes();
    PRISM_CHECK_EQ(blob[0], 'M');
    PRISM_CHECK_EQ(blob[1], 'E');
    PRISM_CHECK_EQ(blob[2], 'S');
    PRISM_CHECK_EQ(blob[3], 'H');

    MeshData dst;
    PRISM_CHECK(MeshData::from_bytes(blob.data(), blob.size(), dst));
    PRISM_CHECK_STR(dst.name, src.name);
    PRISM_CHECK_EQ(dst.vertices.size(), src.vertices.size());
    PRISM_CHECK_EQ(dst.indices.size(), src.indices.size());
    for (std::size_t i = 0; i < src.vertices.size(); ++i) {
        PRISM_CHECK_NEAR(dst.vertices[i].position.x, src.vertices[i].position.x, 1e-6f);
        PRISM_CHECK_NEAR(dst.vertices[i].tangent.w, src.vertices[i].tangent.w, 1e-6f);
        PRISM_CHECK_NEAR(dst.vertices[i].uv.y, src.vertices[i].uv.y, 1e-6f);
    }
    PRISM_CHECK_NEAR(dst.bounds_max.y, src.bounds_max.y, 1e-6f);

    MeshData junk;
    const u8 garbage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    PRISM_CHECK(!MeshData::from_bytes(garbage, 8, junk));
    PRISM_CHECK(!MeshData::from_bytes(blob.data(), 12, junk));   // truncated
}

// =============================================================== textures ==
PRISM_TEST(assets_pixel_format_sizes_are_consistent) {
    PRISM_CHECK_EQ(bits_per_pixel(PixelFormat::RGBA8), 32u);
    PRISM_CHECK_EQ(bits_per_pixel(PixelFormat::ETC2_RGB8), 4u);
    PRISM_CHECK_EQ(bits_per_pixel(PixelFormat::ASTC_4x4), 8u);
    PRISM_CHECK_EQ(image_bytes(PixelFormat::RGBA8, 4, 4), 64u);
    PRISM_CHECK_EQ(image_bytes(PixelFormat::ETC2_RGB8, 4, 4), 8u);
    PRISM_CHECK_EQ(image_bytes(PixelFormat::ETC2_RGB8, 8, 8), 32u);
    PRISM_CHECK_EQ(image_bytes(PixelFormat::ASTC_4x4, 4, 4), 16u);
    // ETC2 at 4 bpp is 8x smaller than RGBA8 - the reason it is the fallback.
    PRISM_CHECK_EQ(image_bytes(PixelFormat::RGBA8, 64, 64) / image_bytes(PixelFormat::ETC2_RGB8, 64, 64), 8u);
    PRISM_CHECK_STR(pixel_format_name(PixelFormat::ETC2_RGB8), "etc2_rgb8");
    PRISM_CHECK_STR(pixel_format_name(PixelFormat::RGBA8), "rgba8");
}

PRISM_TEST(assets_mip_chain_and_nearest_sampling) {
    // A 2x2 image of 0/100/200/60 averages to exactly 90.
    std::vector<u8> small(16);
    const u8 vals[4] = {0, 100, 200, 60};
    for (int i = 0; i < 4; ++i) {
        small[i * 4 + 0] = vals[i]; small[i * 4 + 1] = vals[i];
        small[i * 4 + 2] = vals[i]; small[i * 4 + 3] = 255;
    }
    u32 nw = 0, nh = 0;
    const std::vector<u8> down = box_downsample(small.data(), 2, 2, nw, nh);
    PRISM_CHECK_EQ(nw, 1u);
    PRISM_CHECK_EQ(nh, 1u);
    PRISM_CHECK_EQ(down[0], 90u);
    PRISM_CHECK_EQ(down[3], 255u);

    Texture tex;
    tex.width = 4; tex.height = 4;
    tex.format = PixelFormat::RGBA8;
    tex.pixels = solid_rgba(4, 4, 0, 128, 255);
    PRISM_CHECK_EQ(tex.mip_levels(), 3u);
    tex.build_mip_chain();
    PRISM_CHECK_EQ(tex.mips.size(), 2u);
    PRISM_CHECK_EQ(tex.mips[0].size(), 4u * 4u);          // 2x2 RGBA
    PRISM_CHECK_EQ(tex.mips[1].size(), 4u);               // 1x1 RGBA
    PRISM_CHECK_EQ(tex.mips[1][1], 128u);

    const math::Color c = tex.sample_nearest(0.1f, 0.9f);
    PRISM_CHECK_NEAR(c.g, 128.0f / 255.0f, 1e-5f);
    PRISM_CHECK_NEAR(c.b, 1.0f, 1e-5f);
    // UVs wrap rather than clamping or reading out of bounds.
    PRISM_CHECK_NEAR(tex.sample_nearest(1.25f, -0.25f).b, 1.0f, 1e-5f);
    PRISM_CHECK_NEAR(tex.average_luminance(), (0.7152f * 128.0f + 0.0722f * 255.0f) / 255.0f, 1e-4f);
}

// ==================================================================== ETC ==
PRISM_TEST(assets_etc1_decoder_known_answer_individual_mode) {
    // Hand-built block: individual mode, horizontal split, table A = 0 ({2,8}),
    // table B = 7 ({47,182}), every pixel index 01 = "-small".
    const u8 block[8] = {0xA3, 0x1F, 0x53, 0x1D, 0x00, 0x00, 0xFF, 0xFF};
    u8 rgba[64];
    etc1::decode_block(block, rgba);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const u8* p = &rgba[(y * 4 + x) * 4];
            if (y < 2) {
                PRISM_CHECK_EQ(p[0], 168u);   // 0xA0 -> 170, -2
                PRISM_CHECK_EQ(p[1], 15u);    // 0x10 -> 17,  -2
                PRISM_CHECK_EQ(p[2], 83u);    // 0x50 -> 85,  -2
            } else {
                PRISM_CHECK_EQ(p[0], 4u);     // 0x30 -> 51,  -47
                PRISM_CHECK_EQ(p[1], 208u);   // 0xF0 -> 255, -47
                PRISM_CHECK_EQ(p[2], 4u);
            }
            PRISM_CHECK_EQ(p[3], 255u);
        }
    }
}

PRISM_TEST(assets_etc1_decoder_known_answer_differential_mode) {
    // diff=1, flip=0 (column split), tables {9,29}, every pixel index 00 = "-large".
    // 5-bit base (16,8,4) with a +1/-1/0 delta for sub-block 2.
    const u8 block[8] = {0x81, 0x47, 0x20, 0x4A, 0x00, 0x00, 0x00, 0x00};
    u8 rgba[64];
    etc1::decode_block(block, rgba);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const u8* p = &rgba[(y * 4 + x) * 4];
            if (x < 2) {
                PRISM_CHECK_EQ(p[0], 103u);  // (16<<3)|(16>>2) = 132, -29
                PRISM_CHECK_EQ(p[1], 37u);   // (8<<3)|(8>>2)   = 66,  -29
                PRISM_CHECK_EQ(p[2], 4u);    // (4<<3)|(4>>2)   = 33,  -29
            } else {
                PRISM_CHECK_EQ(p[0], 111u);  // base 17 -> 140, -29
                PRISM_CHECK_EQ(p[1], 28u);   // base 7  -> 57,  -29
                PRISM_CHECK_EQ(p[2], 4u);    // base 4  -> 33,  -29
            }
        }
    }
}

PRISM_TEST(assets_etc1_decoder_wraps_the_five_bit_base) {
    // 5-bit base 31 with delta +1 must wrap to 0 before the 5->8 expansion.
    const u8 block[8] = {0xF9, 0xF9, 0xF9, 0x02, 0x00, 0x00, 0x00, 0x00};
    u8 rgba[64];
    etc1::decode_block(block, rgba);
    PRISM_CHECK_EQ(rgba[0], 247u);           // 255 - 8 (table 0 large)
    const u8* right = &rgba[(0 * 4 + 2) * 4];
    PRISM_CHECK_EQ(right[0], 0u);            // wrapped base 0, clamped from -8
}

PRISM_TEST(assets_etc1_roundtrip_is_faithful) {
    // Solid colours at the extremes must survive exactly.
    PRISM_CHECK_NEAR(etc1::round_trip_error(solid_rgba(4, 4, 0, 0, 0).data(), 4, 4), 0.0f, 1e-4f);
    PRISM_CHECK_NEAR(etc1::round_trip_error(solid_rgba(4, 4, 255, 255, 255).data(), 4, 4), 0.0f, 1e-4f);
    PRISM_CHECK_NEAR(etc1::round_trip_error(solid_rgba(4, 4, 128, 128, 128).data(), 4, 4), 0.0f, 1e-4f);

    // A gentle ramp inside one block should land within a few levels.
    std::vector<u8> ramp(64);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const u8 v = static_cast<u8>(64 + (x + y) * 8);
            ramp[(y * 4 + x) * 4 + 0] = v;
            ramp[(y * 4 + x) * 4 + 1] = v;
            ramp[(y * 4 + x) * 4 + 2] = v;
            ramp[(y * 4 + x) * 4 + 3] = 255;
        }
    const f32 ramp_err = etc1::round_trip_error(ramp.data(), 4, 4);
    PRISM_CHECK(ramp_err < 10.0f);

    // A natural diagonal ramp: channels move together, so one modifier per
    // pixel can serve all three. This is the case ETC1 was designed for.
    std::vector<u8> img(8 * 8 * 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const u8 v = static_cast<u8>((x + y) * 12);
            img[(y * 8 + x) * 4 + 0] = v;
            img[(y * 8 + x) * 4 + 1] = v;
            img[(y * 8 + x) * 4 + 2] = v;
            img[(y * 8 + x) * 4 + 3] = 255;
        }
    PRISM_CHECK_EQ(etc1::encoded_size(8, 8), 32u);
    const std::vector<u8> etc = etc1::encode(img.data(), 8, 8);
    PRISM_CHECK_EQ(etc.size(), 32u);
    const std::vector<u8> back = etc1::decode(etc.data(), 8, 8);
    PRISM_CHECK_EQ(back.size(), img.size());
    for (std::size_t i = 0; i < img.size(); i += 4) PRISM_CHECK_EQ(back[i + 3], 255u);
    PRISM_CHECK(etc1::round_trip_error(img.data(), 8, 8) < 6.0f);

    // A larger natural gradient stays just as accurate.
    std::vector<u8> big(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * 64 + x) * 4;
            big[i + 0] = static_cast<u8>((x * 4) % 256);
            big[i + 1] = static_cast<u8>((y * 4) % 256);
            big[i + 2] = static_cast<u8>(((x + y) * 2) % 256);
            big[i + 3] = 255;
        }
    PRISM_CHECK_EQ(etc1::encoded_size(64, 64), 2048u);   // 16x16 blocks x 8 bytes
    PRISM_CHECK(etc1::round_trip_error(big.data(), 64, 64) < 6.0f);
    // 4 bpp versus RGBA8's 32: the whole reason the fallback path exists.
    PRISM_CHECK_EQ(big.size() / 8u, etc1::encoded_size(64, 64));

    // Adversarial block: red spans 0..96 while green spans 0..32 and blue is
    // flat, so no single modifier suits all three channels at once. ETC1 has
    // no per-channel modifier - that is precisely why ASTC is the primary
    // format and ETC2 is only the GLES fallback.
    std::vector<u8> harsh(8 * 8 * 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            harsh[(y * 8 + x) * 4 + 0] = static_cast<u8>(x * 32);
            harsh[(y * 8 + x) * 4 + 1] = static_cast<u8>(y * 32);
            harsh[(y * 8 + x) * 4 + 2] = 128;
            harsh[(y * 8 + x) * 4 + 3] = 255;
        }
    PRISM_CHECK(etc1::round_trip_error(harsh.data(), 8, 8) < 20.0f);
}

PRISM_TEST(assets_etc1_modifier_table_matches_the_spec) {
    PRISM_CHECK_EQ(etc1::kModifierTable[0][0], 2);
    PRISM_CHECK_EQ(etc1::kModifierTable[0][1], 8);
    PRISM_CHECK_EQ(etc1::kModifierTable[7][0], 47);
    PRISM_CHECK_EQ(etc1::kModifierTable[7][1], 182);
    // Tables are strictly increasing in both columns.
    for (int i = 1; i < 8; ++i) {
        PRISM_CHECK(etc1::kModifierTable[i][0] > etc1::kModifierTable[i - 1][0]);
        PRISM_CHECK(etc1::kModifierTable[i][1] > etc1::kModifierTable[i - 1][1]);
    }
}
