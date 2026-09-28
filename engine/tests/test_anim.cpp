// =====================================================================
//  PRISM ENGINE — tests/test_anim.cpp
//  Skeletons, pose blending, clip sampling/wrapping/events, animator
//  crossfades, and two-bone IK.
// =====================================================================
#include "prism_test.h"
#include "prism/anim/anim.h"

#include <cmath>

using namespace prism;
using namespace prism::anim;

namespace {
bool close(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool closev(const math::Vec3& a, const math::Vec3& b, f32 eps = 1e-3f) {
    return close(a.x, b.x, eps) && close(a.y, b.y, eps) && close(a.z, b.z, eps);
}
Skeleton arm_chain() {
    Skeleton sk;
    sk.add_bone("shoulder", -1, math::Vec3(0, 0, 0));
    sk.add_bone("elbow", 0, math::Vec3(1, 0, 0));
    sk.add_bone("hand", 1, math::Vec3(1, 0, 0));
    return sk;
}
// A pose whose local transforms equal the skeleton's bind pose — the state
// IK assumes before it overwrites rotations. Pose::identity() alone zeroes
// the translations, which would collapse the chain.
Pose bind_pose(const Skeleton& sk) {
    Pose p;
    p.resize(sk.count());
    for (i32 i = 0; i < static_cast<i32>(sk.count()); ++i) {
        p.translations[static_cast<std::size_t>(i)] = sk.bone(i).bind_translation;
        p.rotations[static_cast<std::size_t>(i)] = sk.bone(i).bind_rotation;
        p.scales[static_cast<std::size_t>(i)] = sk.bone(i).bind_scale;
    }
    return p;
}
} // namespace

PRISM_TEST(anim_skeleton_rest_positions_and_lengths) {
    Skeleton sk;
    sk.add_bone("root", -1, math::Vec3(0, 0, 0));
    sk.add_bone("hip", 0, math::Vec3(0, 1, 0));
    sk.add_bone("knee", 1, math::Vec3(0, -0.5f, 0));
    const auto rest = sk.rest_positions();
    PRISM_CHECK_EQ(rest.size(), std::size_t(3));
    PRISM_CHECK(closev(rest[1], math::Vec3(0, 1, 0)));
    PRISM_CHECK(closev(rest[2], math::Vec3(0, 0.5f, 0)));
    PRISM_CHECK(close(sk.bone_length(1), 1.0f));
    PRISM_CHECK(close(sk.bone_length(2), 0.5f));
    PRISM_CHECK_EQ(sk.index_of("knee"), 2);
    PRISM_CHECK_EQ(sk.index_of("nope"), -1);
}

PRISM_TEST(anim_pose_composes_parent_transforms) {
    Skeleton sk = arm_chain();
    Pose p = Pose::identity(sk.count());
    p.translations[0] = math::Vec3(0, 0, 0);
    p.rotations[0] = math::Quat::from_axis_angle(math::Vec3(0, 0, 1), math::Pi * 0.5f);
    p.translations[1] = math::Vec3(1, 0, 0);
    p.translations[2] = math::Vec3(1, 0, 0);
    const auto jp = p.joint_positions(sk);
    // Shoulder rotated 90° about Z turns the local +X child offsets into +Y.
    PRISM_CHECK(closev(jp[0], math::Vec3(0, 0, 0)));
    PRISM_CHECK(closev(jp[1], math::Vec3(0, 1, 0)));
    PRISM_CHECK(closev(jp[2], math::Vec3(0, 2, 0)));
}

PRISM_TEST(anim_blend_and_add_poses) {
    Pose a = Pose::identity(1);
    Pose b = Pose::identity(1);
    a.translations[0] = math::Vec3(0, 0, 0);
    b.translations[0] = math::Vec3(2, 0, 0);
    Pose out;
    blend_pose(a, b, 0.25f, out);
    PRISM_CHECK(closev(out.translations[0], math::Vec3(0.5f, 0, 0)));
    blend_pose(a, b, 1.0f, out);
    PRISM_CHECK(closev(out.translations[0], math::Vec3(2, 0, 0)));

    // Additive: delta applied at half weight.
    Pose add = Pose::identity(1);
    add.translations[0] = math::Vec3(0, 1, 0);
    add.rotations[0] = math::Quat::from_axis_angle(math::Vec3(0, 1, 0), math::Pi * 0.5f);
    add_pose(a, add, 0.5f, out);
    PRISM_CHECK(closev(out.translations[0], math::Vec3(0, 0.5f, 0)));
    const math::Vec3 dir = out.rotations[0].rotate(math::Vec3(0, 0, 1));
    PRISM_CHECK(close(dir.x, std::sin(math::Pi * 0.25f), 1e-2f));
    PRISM_CHECK(close(dir.z, std::cos(math::Pi * 0.25f), 1e-2f));
}

PRISM_TEST(anim_track_sampling_keeps_keys_sorted) {
    BoneTrack tr;
    tr.bone = 0;
    tr.add_position(1.0f, math::Vec3(10, 0, 0));
    tr.add_position(0.0f, math::Vec3(0, 0, 0));
    tr.add_rotation(0.0f, math::Quat());
    tr.add_rotation(2.0f, math::Quat::from_axis_angle(math::Vec3(0, 1, 0), math::Pi));
    PRISM_CHECK_EQ(tr.position.size(), std::size_t(2));
    PRISM_CHECK(close(tr.position.front().t, 0.0f));      // insert order sorted
    PRISM_CHECK(close(tr.duration(), 2.0f));

    math::Vec3 p; math::Quat r; math::Vec3 s;
    tr.sample(0.5f, p, r, s);
    PRISM_CHECK(closev(p, math::Vec3(5, 0, 0)));          // midpoint of positions
    tr.sample(-1.0f, p, r, s);
    PRISM_CHECK(closev(p, math::Vec3(0, 0, 0)));          // clamps before first
    tr.sample(99.0f, p, r, s);
    PRISM_CHECK(closev(p, math::Vec3(10, 0, 0)));         // clamps after last
    tr.sample(1.0f, p, r, s);
    const math::Vec3 dir = r.rotate(math::Vec3(1, 0, 0));
    // Half of a 180° Y swing is a 90° Y rotation: +X maps onto +/-Z (the sign
    // depends on slerp's shortest-arc choice, so assert on magnitude).
    PRISM_CHECK(close(std::fabs(dir.z), 1.0f, 1e-3f) && close(dir.x, 0.0f, 1e-3f));
    PRISM_CHECK(closev(s, math::Vec3(1, 1, 1)));          // no scale keys -> 1
}

PRISM_TEST(anim_clip_wraps_and_fires_events_across_loop) {
    Skeleton sk = arm_chain();
    Clip c;
    c.name = "walk";
    c.length = 2.0f;
    c.loop = true;
    auto& t = c.track(1);
    t.add_position(0.0f, math::Vec3(1, 0, 0));
    t.add_position(2.0f, math::Vec3(1, 0.5f, 0));
    c.add_event(0.25f, "step_l");
    c.add_event(1.75f, "step_r");

    PRISM_CHECK(close(c.wrap(2.5f), 0.5f));
    PRISM_CHECK(close(c.wrap(-0.5f), 1.5f));
    Pose pose;
    c.sample(sk, 1.0f, pose);
    PRISM_CHECK(closev(pose.translations[1], math::Vec3(1, 0.25f, 0)));
    PRISM_CHECK(closev(pose.translations[0], math::Vec3(0, 0, 0)));  // untracked -> bind

    const auto mid = c.events_between(0.1f, 0.3f);
    PRISM_CHECK_EQ(mid.size(), std::size_t(1));
    PRISM_CHECK(mid[0] == "step_l");
    // Crossing the seam forward from 1.9 through 2.0/0.0 to 0.3 covers
    // (1.9,2.0]u[0.0,0.3]; step_r@1.75 sits before the window, step_l@0.25 in it.
    const auto wrapped = c.events_between(1.9f, 0.3f);
    PRISM_CHECK_EQ(wrapped.size(), std::size_t(1));
    PRISM_CHECK(wrapped[0] == "step_l");
    // A wider seam window (1.5 -> 0.3) does sweep in step_r@1.75 too. Events
    // come back in ascending-time order, so step_l@0.25 precedes step_r@1.75.
    const auto both = c.events_between(1.5f, 0.3f);
    PRISM_CHECK_EQ(both.size(), std::size_t(2));
    PRISM_CHECK(both[0] == "step_l");
    PRISM_CHECK(both[1] == "step_r");

    c.loop = false;
    PRISM_CHECK(close(c.wrap(99.0f), 2.0f));
    PRISM_CHECK_EQ(c.events_between(1.9f, 99.0f).size(), std::size_t(0));
}

PRISM_TEST(anim_animator_crossfade_and_events) {
    Skeleton sk = arm_chain();
    Clip a; a.name = "idle"; a.length = 1.0f; a.add_event(0.5f, "tick");
    a.track(1).add_position(0.0f, math::Vec3(1, 0, 0));
    Clip b; b.name = "run"; b.length = 1.0f;
    b.track(1).add_position(0.0f, math::Vec3(1, 1, 0));

    Animator an(sk);
    an.play(a, 0.0f);
    auto fired = an.update(0.6f);
    PRISM_CHECK(an.current() == "idle");
    PRISM_CHECK_EQ(fired.size(), std::size_t(1));
    PRISM_CHECK(fired[0] == "tick");

    an.crossfade(b, 0.2f);
    an.update(0.1f);                                   // halfway through fade
    const f32 y_mid = an.pose().translations[1].y;
    PRISM_CHECK(y_mid > 0.1f && y_mid < 0.9f);
    an.update(0.5f);                                   // fade finished
    PRISM_CHECK(an.current() == "run");
    PRISM_CHECK(close(an.pose().translations[1].y, 1.0f));
    an.set_speed(0.0f);
    const f32 frozen = an.time();
    an.update(1.0f);
    PRISM_CHECK(close(an.time(), frozen));
}

PRISM_TEST(anim_two_bone_ik_reaches_and_stretches) {
    const math::Vec3 root(0, 0, 0);
    auto s = solve_two_bone(root, math::Vec3(1.5f, 0, 0), math::Vec3(0, 1, 0), 1.0f, 1.0f);
    PRISM_CHECK(s.reachable);
    PRISM_CHECK(closev(s.end, math::Vec3(1.5f, 0, 0)));
    PRISM_CHECK(close((s.elbow - root).length(), 1.0f));
    PRISM_CHECK(close((s.end - s.elbow).length(), 1.0f));
    PRISM_CHECK(s.elbow.y > 0.0f);                     // pole hint bends it up

    auto far = solve_two_bone(root, math::Vec3(5, 0, 0), math::Vec3(0, 1, 0), 1.0f, 1.0f);
    PRISM_CHECK(!far.reachable);
    PRISM_CHECK(closev(far.end, math::Vec3(2, 0, 0), 1e-2f));   // fully stretched

    // apply_two_bone drives the pose so the hand lands on the target.
    Skeleton sk = arm_chain();
    Pose p = bind_pose(sk);
    apply_two_bone(sk, p, 0, 1, 2, math::Vec3(1.5f, 0.5f, 0), math::Vec3(0, 1, 0));
    const auto jp = p.joint_positions(sk);
    PRISM_CHECK(closev(jp[2], math::Vec3(1.5f, 0.5f, 0), 1e-2f));
    PRISM_CHECK(close((jp[1] - jp[0]).length(), 1.0f, 1e-2f));
}
