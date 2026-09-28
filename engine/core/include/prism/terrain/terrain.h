// =====================================================================
//  PRISM ENGINE — terrain/terrain.h
//  Heightfield terrain with the sculpting brushes the editors show
//  (raise / lower, smooth, flatten), bilinear sampling, analytic-ish
//  normals, vertical ray marching, mesh export through assets::MeshData,
//  and a foliage density painter that scatters deterministic instances.
//  Everything is a flat f32 grid, so it is trivially serialisable into a
//  .prism pack and trivially testable on the host.
// =====================================================================
#pragma once
#include <cstdint>
#include <vector>
#include "../assets/assets.h"
#include "../core/types.h"
#include "../math/math.h"

namespace prism::terrain {

class Heightmap {
public:
    Heightmap() = default;
    /// resolution vertices per side; size = world units covered.
    Heightmap(i32 resolution, f32 size, math::Vec3 origin = math::Vec3());

    [[nodiscard]] i32 resolution() const { return n_; }
    [[nodiscard]] f32 size() const { return size_; }
    [[nodiscard]] f32 cell() const { return cell_; }
    [[nodiscard]] const math::Vec3& origin() const { return origin_; }
    [[nodiscard]] bool inside(f32 x, f32 z) const;

    /// Bilinear height sample at world (x, z).
    [[nodiscard]] f32 height_at(f32 x, f32 z) const;
    [[nodiscard]] math::Vec3 normal_at(f32 x, f32 z) const;
    /// Raw vertex height (grid indices).
    [[nodiscard]] f32 vertex(i32 x, i32 z) const;

    // ----------------------------------------------------------- brushes ---
    /// Signed strength: positive raises, negative lowers. Cosine falloff.
    void raise(f32 x, f32 z, f32 radius, f32 strength);
    void smooth(f32 x, f32 z, f32 radius, f32 amount);
    void flatten(f32 x, f32 z, f32 radius, f32 target, f32 amount);

    /// Marches the ray and refines the crossing with bisection.
    [[nodiscard]] bool raycast(math::Vec3 from, math::Vec3 dir, f32 max_distance,
                               math::Vec3& out_point) const;

    /// Triangle mesh, one vertex per `step` grid cells.
    [[nodiscard]] assets::MeshData build_mesh(i32 step = 1, const std::string& name = "terrain") const;

    [[nodiscard]] f32 min_height() const;
    [[nodiscard]] f32 max_height() const;

private:
    f32& at(i32 x, i32 z) { return h_[static_cast<std::size_t>(z) * n_ + x]; }
    f32 at(i32 x, i32 z) const { return h_[static_cast<std::size_t>(z) * n_ + x]; }

    i32 n_ = 0;
    f32 size_ = 0, cell_ = 1;
    math::Vec3 origin_;
    std::vector<f32> h_;
};

struct FoliageInstance {
    math::Vec3 position;
    f32 yaw = 0;
    f32 scale = 1;
    u32 type = 0;
};

class FoliagePainter {
public:
    explicit FoliagePainter(const Heightmap& hm);
    void paint(f32 x, f32 z, f32 radius, f32 amount);
    void erase(f32 x, f32 z, f32 radius, f32 amount);
    void clear();
    [[nodiscard]] f32 density_at(f32 x, f32 z) const;

    /// One instance (at most) per painted cell, jittered deterministically.
    [[nodiscard]] std::vector<FoliageInstance> scatter(u32 seed, u32 type_count = 1) const;
    [[nodiscard]] f32 painted_area() const;

private:
    const Heightmap* hm_;
    std::vector<f32> density_;
};

} // namespace prism::terrain
