#include "prism_test.h"
#include "prism/math/math.h"
#include "prism/core/pool.h"

using namespace prism;
using namespace prism::math;

PRISM_TEST(math_vec2_basics) {
    Vec2 a{3, 4};
    PRISM_CHECK_NEAR(a.length(), 5.0, 1e-5);
    PRISM_CHECK_NEAR(a.dot(Vec2(1, 0)), 3.0, 1e-5);
    PRISM_CHECK_NEAR(a.cross(Vec2(0, 1)), 3.0, 1e-5);
    PRISM_CHECK(a.normalized() == Vec2(0.6f, 0.8f));
    PRISM_CHECK((a + Vec2(1, 1)) == Vec2(4, 5));
    PRISM_CHECK(Vec2(1, 2).perpendicular() == Vec2(-2, 1));
}

PRISM_TEST(math_vec3_cross_handedness) {
    PRISM_CHECK(Vec3::right().cross(Vec3::up()) == Vec3(0, 0, 1));
    PRISM_CHECK_NEAR(Vec3(1, 2, 3).distance(Vec3(1, 2, 3)), 0.0, 1e-6);
    Vec3 r = Vec3(1, 0, 0).reflect(Vec3(0, 1, 0));
    PRISM_CHECK(r == Vec3(1, 0, 0));
}

PRISM_TEST(math_mat4_transform_roundtrip) {
    Mat4 m = Mat4::translate({10, 20, 30}) * Mat4::scale({2, 2, 2});
    Vec3 p = m.transform_point({1, 1, 1});
    PRISM_CHECK_NEAR(p.x, 12.0, 1e-4);
    PRISM_CHECK_NEAR(p.y, 22.0, 1e-4);
    PRISM_CHECK_NEAR(p.z, 32.0, 1e-4);
    Vec3 d = m.transform_dir({1, 0, 0});
    PRISM_CHECK_NEAR(d.x, 2.0, 1e-4);   // direction ignores translation
    PRISM_CHECK_NEAR(d.y, 0.0, 1e-4);
}

static double clip_at(const Mat4& p, float z_eye) {
    Vec4 c = p * Vec4(Vec3(0, 0, z_eye), 1.0f);
    return static_cast<double>(c.z) / static_cast<double>(c.w);
}

PRISM_TEST(math_mat4_projection_clip) {
    Mat4 p = Mat4::perspective(60 * Deg2Rad, 16.0f / 9.0f, 0.1f, 100.0f, true);
    PRISM_CHECK((p * Vec4(Vec3(0, 0, -10), 1.0f)).w > 0.0f);   // in front of camera
    // Analytic zero-to-one depth: 0 at near plane, 1 at far plane (verified offline).
    PRISM_CHECK_NEAR((p * Vec4(Vec3(0, 0, -0.1f), 1.0f)).z / (p * Vec4(Vec3(0, 0, -0.1f), 1.0f)).w, 0.0, 1e-4);
    PRISM_CHECK_NEAR((p * Vec4(Vec3(0, 0, -100.0f), 1.0f)).z / (p * Vec4(Vec3(0, 0, -100.0f), 1.0f)).w, 1.0, 1e-4);
    PRISM_CHECK_NEAR(clip_at(p, -10.0f), 0.990991, 1e-5);
}

PRISM_TEST(math_quat_rotation) {
    Quat q = Quat::from_axis_angle(Vec3::up(), HalfPi);
    Vec3 v = q.rotate(Vec3(1, 0, 0));
    PRISM_CHECK_NEAR(v.x, 0.0, 1e-5);
    PRISM_CHECK_NEAR(v.z, -1.0, 1e-5);
    Mat4 m = q.to_mat4();
    Vec3 v2 = m.transform_dir(Vec3(1, 0, 0));
    PRISM_CHECK_NEAR(v2.z, -1.0, 1e-5);
    Quat mid = Quat::slerp(Quat{}, q, 0.5f);
    Vec3 v3 = mid.rotate(Vec3(1, 0, 0));
    PRISM_CHECK_NEAR(v3.x, std::cos(Pi / 4.0), 1e-5);
}

PRISM_TEST(math_rect_intersection) {
    Rect a{0, 0, 10, 10}, b{5, 5, 10, 10};
    PRISM_CHECK(a.intersects(b));
    PRISM_CHECK_NEAR(a.intersection(b).area(), 25.0, 1e-5);
    PRISM_CHECK(a.contains(Vec2(1, 1)));
    PRISM_CHECK(!a.contains(Vec2(11, 1)));
}

PRISM_TEST(math_color_brand_palette) {
    PRISM_CHECK_EQ(Color::prism_violet().to_rgba8(), 0x7C3AEDFFu);
    PRISM_CHECK_EQ(Color::spectrum_cyan().to_rgba8(), 0x00D9FFFFu);
    PRISM_CHECK_EQ(Color::prism_gold().to_rgba8(), 0xFFC93CFFu);
    PRISM_CHECK_EQ(Color::sunset_magenta().to_rgba8(), 0xFF3D9AFFu);
    PRISM_CHECK_EQ(Color::dark_bg().to_rgba8(), 0x0A0A12FFu);
}

PRISM_TEST(core_linear_allocator_alignment) {
    LinearAllocator a(1024);
    void* p1 = a.allocate(10);
    void* p2 = a.allocate(10);
    PRISM_CHECK(p1 != nullptr && p2 != nullptr);
    PRISM_CHECK((reinterpret_cast<std::uintptr_t>(p1) & 15) == 0);
    PRISM_CHECK((reinterpret_cast<std::uintptr_t>(p2) & 15) == 0);
    PRISM_CHECK_EQ(a.used(), std::size_t(26));       // 16 (aligned) + 10
    a.rewind();
    PRISM_CHECK_EQ(a.used(), std::size_t(0));
    PRISM_CHECK(a.peak() >= 26);                    // peak retained after rewind
    LinearAllocator small(16);
    PRISM_CHECK(small.allocate(64) == nullptr);   // overflow returns null, never UB
    PRISM_CHECK_EQ(small.overflows(), u64(1));
}

PRISM_TEST(core_object_pool_reuse) {
    struct Bullet { int id = 0; };
    ObjectPool<Bullet, 8> pool;
    std::vector<Bullet*> live;
    for (int i = 0; i < 20; ++i) live.push_back(pool.acquire());
    PRISM_CHECK_EQ(pool.live(), std::size_t(20));
    for (auto* b : live) pool.release(b);
    PRISM_CHECK_EQ(pool.live(), std::size_t(0));
    Bullet* again = pool.acquire();
    PRISM_CHECK(again != nullptr);
    PRISM_CHECK(pool.reserved() >= 24);           // grew in blocks, never shrank
}
