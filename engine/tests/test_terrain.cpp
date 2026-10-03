// =====================================================================
//  PRISM ENGINE — tests/test_terrain.cpp
//  Sculpting brushes, bilinear sampling, normals, ray marching, mesh export
//  and the foliage density painter / deterministic scatter.
// =====================================================================
#include "prism_test.h"
#include "prism/terrain/terrain.h"

#include <cmath>

using namespace prism;
using namespace prism::terrain;

namespace {
bool close(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
} // namespace

PRISM_TEST(terrain_raise_brush_shapes_the_heightfield) {
    Heightmap hm(17, 16.0f);                    // 1 unit per cell
    hm.raise(8.0f, 8.0f, 3.0f, 2.0f);
    PRISM_CHECK(close(hm.height_at(8.0f, 8.0f), 2.0f, 1e-2f));   // brush centre
    // falloff(2,3) = 0.5*(1+cos(2pi/3)) = 0.25 -> vertex (8,6) holds 0.5, so
    // the bilinear midpoint between it and the zero ring is 0.25.
    PRISM_CHECK(close(hm.height_at(8.0f, 5.5f), 0.25f, 0.05f));
    PRISM_CHECK(close(hm.height_at(0.0f, 0.0f), 0.0f));          // untouched corner
    PRISM_CHECK(hm.max_height() > 1.9f);
    PRISM_CHECK(close(hm.min_height(), 0.0f));
    hm.raise(8.0f, 8.0f, 2.0f, -1.0f);
    PRISM_CHECK(hm.height_at(8.0f, 8.0f) < 2.0f);                // negative digs
}

PRISM_TEST(terrain_bilinear_sampling_between_vertices) {
    Heightmap hm(5, 4.0f);                      // 1 unit per cell
    hm.raise(2.0f, 2.0f, 0.1f, 4.0f);           // single hot vertex
    PRISM_CHECK(close(hm.vertex(2, 2), 4.0f, 1e-2f));
    PRISM_CHECK(close(hm.height_at(2.0f, 2.0f), 4.0f, 1e-2f));
    PRISM_CHECK(close(hm.height_at(2.5f, 2.0f), 2.0f, 1e-2f));   // halfway to 0
    PRISM_CHECK(close(hm.height_at(2.5f, 2.5f), 1.0f, 1e-2f));   // quarter
    PRISM_CHECK(hm.inside(3.9f, 0.1f) && !hm.inside(4.1f, 0.0f));
}

PRISM_TEST(terrain_normals_up_on_flats_tilted_on_slopes) {
    Heightmap flat(9, 8.0f);
    const math::Vec3 n = flat.normal_at(4.0f, 4.0f);
    PRISM_CHECK(close(n.y, 1.0f, 1e-3f));
    Heightmap slope(9, 8.0f);
    for (i32 x = 0; x < 9; ++x)
        slope.raise(static_cast<f32>(x), 4.0f, 0.1f, static_cast<f32>(x));  // ramp in +x
    const math::Vec3 s = slope.normal_at(4.0f, 4.0f);
    PRISM_CHECK(s.y > 0.0f && s.y < 1.0f);
    PRISM_CHECK(s.x < 0.0f);                   // tilts against the uphill
}

PRISM_TEST(terrain_smooth_and_flatten_brushes) {
    Heightmap hm(17, 16.0f);
    hm.raise(8.0f, 8.0f, 0.1f, 5.0f);          // spike
    const f32 peak = hm.height_at(8.0f, 8.0f);
    hm.smooth(8.0f, 8.0f, 3.0f, 1.0f);
    PRISM_CHECK(hm.height_at(8.0f, 8.0f) < peak - 1.0f);
    PRISM_CHECK(hm.height_at(9.0f, 8.0f) > 0.0f);              // spread outward

    hm.flatten(8.0f, 8.0f, 6.0f, 1.0f, 1.0f);
    PRISM_CHECK(close(hm.height_at(8.0f, 8.0f), 1.0f, 1e-3f));       // centre: full weight
    PRISM_CHECK(close(hm.height_at(7.0f, 8.0f), 1.0f, 0.05f));       // neighbour: falloff weight
}

PRISM_TEST(terrain_raycast_marches_down_onto_a_hill) {
    Heightmap hm(33, 32.0f);
    hm.raise(16.0f, 16.0f, 4.0f, 6.0f);
    math::Vec3 hit;
    PRISM_CHECK(hm.raycast(math::Vec3(16.0f, 20.0f, 16.0f), math::Vec3(0, -1, 0), 40.0f, hit));
    PRISM_CHECK(close(hit.y, hm.height_at(16.0f, 16.0f), 0.05f));
    PRISM_CHECK(close(hit.x, 16.0f, 0.01f));
    PRISM_CHECK(!hm.raycast(math::Vec3(16.0f, 20.0f, 16.0f), math::Vec3(0, 1, 0), 40.0f, hit));
}

PRISM_TEST(terrain_mesh_export_counts_and_bounds) {
    Heightmap hm(9, 8.0f);
    hm.raise(4.0f, 4.0f, 2.0f, 3.0f);
    const auto m = hm.build_mesh(1);
    PRISM_CHECK_EQ(m.vertices.size(), std::size_t(81));        // 9x9 verts
    PRISM_CHECK_EQ(m.indices.size(), std::size_t(6 * 64));     // 2 tris per quad
    PRISM_CHECK(close(m.bounds_max.y, hm.max_height(), 1e-4f));
    const auto coarse = hm.build_mesh(4);
    PRISM_CHECK_EQ(coarse.vertices.size(), std::size_t(9));    // 3x3 sampled grid
    PRISM_CHECK_EQ(coarse.indices.size(), std::size_t(6 * 4));
}

PRISM_TEST(foliage_paint_scatter_is_bounded_and_deterministic) {
    Heightmap hm(33, 32.0f);
    hm.raise(16.0f, 16.0f, 8.0f, 2.0f);
    FoliagePainter fp(hm);
    fp.paint(16.0f, 16.0f, 4.0f, 1.0f);
    PRISM_CHECK(fp.density_at(16.0f, 16.0f) > 0.99f);
    PRISM_CHECK(close(fp.density_at(0.0f, 0.0f), 0.0f));
    PRISM_CHECK(fp.painted_area() > 0.0f);

    const auto a = fp.scatter(99, 3);
    const auto b = fp.scatter(99, 3);
    PRISM_CHECK(!a.empty());
    PRISM_CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        PRISM_CHECK_EQ(a[i].type, b[i].type);
        PRISM_CHECK(close(a[i].position.y, hm.height_at(a[i].position.x, a[i].position.z), 1e-3f));
        PRISM_CHECK(a[i].scale >= 0.8f && a[i].scale <= 1.2f);
        const f32 dx = a[i].position.x - 16.0f, dz = a[i].position.z - 16.0f;
        PRISM_CHECK(std::sqrt(dx * dx + dz * dz) <= 5.0f);     // inside the painted brush
        PRISM_CHECK(a[i].type < 3);
    }
    const auto c = fp.scatter(7, 3);
    bool differs = c.size() != a.size();
    for (std::size_t i = 0; !differs && i < a.size(); ++i) differs = differs || a[i].type != c[i].type;
    PRISM_CHECK(differs);

    fp.erase(16.0f, 16.0f, 4.0f, 1.0f);
    PRISM_CHECK(close(fp.density_at(16.0f, 16.0f), 0.0f));
    PRISM_CHECK_EQ(fp.scatter(99, 3).size(), std::size_t(0));
}
