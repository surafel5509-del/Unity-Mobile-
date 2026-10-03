// =====================================================================
//  PRISM ENGINE — anim/anim.h
//  Skeletal animation: bones, clips with keyframe tracks, pose sampling
//  and blending, animation events, and analytic two-bone IK. Mirrors the
//  Animator / timeline concepts from the reference editors but stays a
//  deterministic, host-testable data + math layer (no GPU skinning here).
// =====================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::anim {

// ------------------------------------------------------------------ bone ---
struct Bone {
    std::string name;
    i32 parent = -1;                 // -1 = root
    math::Vec3 bind_translation;
    math::Quat bind_rotation;
    math::Vec3 bind_scale = math::Vec3(1, 1, 1);
};

class Skeleton {
public:
    i32 add_bone(std::string name, i32 parent,
                 math::Vec3 t = math::Vec3(), math::Quat r = math::Quat(),
                 math::Vec3 s = math::Vec3(1, 1, 1));
    [[nodiscard]] i32 index_of(std::string_view name) const;
    [[nodiscard]] const Bone& bone(i32 i) const { return bones_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] std::size_t count() const { return bones_.size(); }
    /// World-space rest positions, root to leaf.
    [[nodiscard]] std::vector<math::Vec3> rest_positions() const;
    [[nodiscard]] f32 bone_length(i32 child) const;      // distance to parent
private:
    std::vector<Bone> bones_;
};

// ------------------------------------------------------------------ pose ---
/// One sampled pose: a local transform per bone, indexed like the skeleton.
struct Pose {
    std::vector<math::Vec3> translations;
    std::vector<math::Quat> rotations;
    std::vector<math::Vec3> scales;
    void resize(std::size_t n);
    [[nodiscard]] std::size_t size() const { return translations.size(); }
    static Pose identity(std::size_t n);
    /// World positions of every joint for the current local transforms.
    [[nodiscard]] std::vector<math::Vec3> joint_positions(const Skeleton& sk) const;
};
/// Linear blend: `out = lerp(a, b, t)` component-wise (quats use slerp).
void blend_pose(const Pose& a, const Pose& b, f32 t, Pose& out);
/// Additive: applies the delta (base->add) on top of `base`.
void add_pose(const Pose& base, const Pose& add, f32 weight, Pose& out);

// ----------------------------------------------------------------- track ---
struct Vec3Key { f32 t; math::Vec3 v; };
struct QuatKey { f32 t; math::Quat q; };

struct BoneTrack {
    i32 bone = 0;
    std::vector<Vec3Key> position;
    std::vector<QuatKey> rotation;
    std::vector<Vec3Key> scale;
    /// Keys must be time-sorted; insertion keeps them so.
    void add_position(f32 t, math::Vec3 v);
    void add_rotation(f32 t, math::Quat q);
    void add_scale(f32 t, math::Vec3 v);
    void sample(f32 t, math::Vec3& p, math::Quat& r, math::Vec3& s) const;
    [[nodiscard]] f32 duration() const;
};

struct AnimEvent { f32 time; std::string name; };

// ------------------------------------------------------------------ clip ---
struct Clip {
    std::string name;
    f32 length = 0;
    bool loop = true;
    std::vector<BoneTrack> tracks;
    std::vector<AnimEvent> events;
    BoneTrack& track(i32 bone);
    void add_event(f32 t, std::string name);
    /// Sample the clip at local time (wrapping if it loops).
    void sample(const Skeleton& sk, f32 time, Pose& out) const;
    /// Event names fired in (from, to], handling loop wraparound.
    [[nodiscard]] std::vector<std::string> events_between(f32 from, f32 to) const;
    [[nodiscard]] f32 wrap(f32 t) const;
};

// ------------------------------------------------------------ animation ----
/// A lightweight animation state machine: plays clips with crossfades.
class Animator {
public:
    explicit Animator(const Skeleton& sk);
    void play(const Clip& clip, f32 fade_in = 0.0f);
    void crossfade(const Clip& clip, f32 duration);
    /// Advance time, returning the events fired this step.
    std::vector<std::string> update(f32 dt);
    [[nodiscard]] const Pose& pose() const { return pose_; }
    [[nodiscard]] f32 time() const { return time_; }
    [[nodiscard]] const std::string& current() const { return current_ ? current_->name : empty_; }
    void set_speed(f32 s) { speed_ = s; }
private:
    const Skeleton& sk_;
    const Clip* current_ = nullptr;
    const Clip* from_ = nullptr;
    f32 time_ = 0, from_time_ = 0, fade_ = 0, fade_t_ = 0, speed_ = 1;
    Pose pose_;
    std::string empty_;
};

// -------------------------------------------------------------------- IK ---
/// Analytic two-bone IK. Given a root position, a target, a pole (bend) hint
/// and the two bone lengths, solves elbow and end positions. The end lands on
/// the target when reachable, otherwise fully stretched toward it.
struct TwoBoneResult { math::Vec3 elbow; math::Vec3 end; bool reachable; };
[[nodiscard]] TwoBoneResult solve_two_bone(math::Vec3 root, math::Vec3 target,
                                           math::Vec3 pole, f32 l1, f32 l2);
/// Applies a two-bone solution back onto a pose's rotations for the chain
/// (root -> child -> grandchild), aiming each bone at its solved joint.
void apply_two_bone(const Skeleton& sk, Pose& pose, i32 root, i32 child, i32 end,
                    math::Vec3 target, math::Vec3 pole);

} // namespace prism::anim
