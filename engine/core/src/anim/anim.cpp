// =====================================================================
//  PRISM ENGINE — anim/anim.cpp
// =====================================================================
#include "prism/anim/anim.h"

#include <algorithm>
#include <cmath>

namespace prism::anim {

namespace {
math::Quat quat_from_to(math::Vec3 a, math::Vec3 b) {
    a = a.normalized();
    b = b.normalized();
    const f32 d = a.dot(b);
    if (d > 0.9999f) return math::Quat();
    if (d < -0.9999f) {
        math::Vec3 axis = a.cross(math::Vec3(1, 0, 0));
        if (axis.length() < 1e-4f) axis = a.cross(math::Vec3(0, 1, 0));
        return math::Quat::from_axis_angle(axis.normalized(), math::Pi);
    }
    const math::Vec3 axis = a.cross(b);
    math::Quat q(axis.x, axis.y, axis.z, 1.0f + d);
    return q.normalized();
}
} // namespace

// =============================================================== skeleton ==
i32 Skeleton::add_bone(std::string name, i32 parent, math::Vec3 t, math::Quat r, math::Vec3 s) {
    Bone b;
    b.name = std::move(name);
    b.parent = parent;
    b.bind_translation = t;
    b.bind_rotation = r;
    b.bind_scale = s;
    bones_.push_back(std::move(b));
    return static_cast<i32>(bones_.size()) - 1;
}

i32 Skeleton::index_of(std::string_view name) const {
    for (std::size_t i = 0; i < bones_.size(); ++i)
        if (bones_[i].name == name) return static_cast<i32>(i);
    return -1;
}

std::vector<math::Vec3> Skeleton::rest_positions() const {
    std::vector<math::Vec3> out(bones_.size());
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        out[i] = bones_[i].bind_translation;
        if (bones_[i].parent >= 0) out[i] = out[i] + out[static_cast<std::size_t>(bones_[i].parent)];
    }
    return out;
}

f32 Skeleton::bone_length(i32 child) const {
    if (child < 0 || child >= static_cast<i32>(bones_.size())) return 0.0f;
    const i32 p = bones_[static_cast<std::size_t>(child)].parent;
    if (p < 0) return bones_[static_cast<std::size_t>(child)].bind_translation.length();
    return (rest_positions()[static_cast<std::size_t>(child)] -
            rest_positions()[static_cast<std::size_t>(p)]).length();
}

// =================================================================== pose ==
void Pose::resize(std::size_t n) {
    translations.resize(n);
    rotations.resize(n);
    scales.resize(n, math::Vec3(1, 1, 1));
}

Pose Pose::identity(std::size_t n) {
    Pose p;
    p.resize(n);
    return p;
}

std::vector<math::Vec3> Pose::joint_positions(const Skeleton& sk) const {
    std::vector<math::Vec3> world_pos(sk.count());
    std::vector<math::Quat> world_rot(sk.count());
    for (std::size_t i = 0; i < sk.count(); ++i) {
        const Bone& b = sk.bone(static_cast<i32>(i));
        math::Vec3 pos = translations[i];
        math::Quat rot = rotations[i];
        if (b.parent >= 0) {
            const std::size_t p = static_cast<std::size_t>(b.parent);
            pos = world_pos[p] + world_rot[p].rotate(pos);
            rot = world_rot[p] * rot;
        }
        world_pos[i] = pos;
        world_rot[i] = rot.normalized();
    }
    return world_pos;
}

void blend_pose(const Pose& a, const Pose& b, f32 t, Pose& out) {
    const std::size_t n = std::min(a.size(), b.size());
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.translations[i] = a.translations[i].lerp(b.translations[i], t);
        out.rotations[i] = math::Quat::slerp(a.rotations[i], b.rotations[i], t);
        out.scales[i] = a.scales[i].lerp(b.scales[i], t);
    }
}

void add_pose(const Pose& base, const Pose& add, f32 weight, Pose& out) {
    const std::size_t n = std::min(base.size(), add.size());
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.translations[i] = base.translations[i] + add.translations[i] * weight;
        out.rotations[i] = (base.rotations[i] *
                            math::Quat::slerp(math::Quat(), add.rotations[i], weight)).normalized();
        out.scales[i] = base.scales[i] * (math::Vec3(1, 1, 1).lerp(add.scales[i], weight));
    }
}

// ================================================================== track ==
void BoneTrack::add_position(f32 t, math::Vec3 v) {
    position.push_back(Vec3Key{t, v});
    std::sort(position.begin(), position.end(), [](const Vec3Key& a, const Vec3Key& b) { return a.t < b.t; });
}
void BoneTrack::add_rotation(f32 t, math::Quat q) {
    rotation.push_back(QuatKey{t, q});
    std::sort(rotation.begin(), rotation.end(), [](const QuatKey& a, const QuatKey& b) { return a.t < b.t; });
}
void BoneTrack::add_scale(f32 t, math::Vec3 v) {
    scale.push_back(Vec3Key{t, v});
    std::sort(scale.begin(), scale.end(), [](const Vec3Key& a, const Vec3Key& b) { return a.t < b.t; });
}

f32 BoneTrack::duration() const {
    f32 d = 0;
    for (const auto& k : position) d = std::max(d, k.t);
    for (const auto& k : rotation) d = std::max(d, k.t);
    for (const auto& k : scale) d = std::max(d, k.t);
    return d;
}

namespace {
math::Vec3 sample_vec(const std::vector<Vec3Key>& keys, f32 t, math::Vec3 fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().t) return keys.front().v;
    if (t >= keys.back().t) return keys.back().v;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const f32 span = keys[i].t - keys[i - 1].t;
            const f32 f = span <= 0.0f ? 0.0f : (t - keys[i - 1].t) / span;
            return keys[i - 1].v.lerp(keys[i].v, f);
        }
    }
    return keys.back().v;
}
math::Quat sample_quat(const std::vector<QuatKey>& keys, f32 t, math::Quat fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().t) return keys.front().q;
    if (t >= keys.back().t) return keys.back().q;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const f32 span = keys[i].t - keys[i - 1].t;
            const f32 f = span <= 0.0f ? 0.0f : (t - keys[i - 1].t) / span;
            return math::Quat::slerp(keys[i - 1].q, keys[i].q, f);
        }
    }
    return keys.back().q;
}
} // namespace

void BoneTrack::sample(f32 t, math::Vec3& p, math::Quat& r, math::Vec3& s) const {
    p = sample_vec(position, t, math::Vec3());
    r = sample_quat(rotation, t, math::Quat());
    s = sample_vec(scale, t, math::Vec3(1, 1, 1));
}

// =================================================================== clip ==
BoneTrack& Clip::track(i32 bone) {
    for (auto& t : tracks) if (t.bone == bone) return t;
    tracks.push_back(BoneTrack{});
    tracks.back().bone = bone;
    return tracks.back();
}

void Clip::add_event(f32 t, std::string name) {
    events.push_back(AnimEvent{t, std::move(name)});
    std::sort(events.begin(), events.end(), [](const AnimEvent& a, const AnimEvent& b) { return a.time < b.time; });
}

f32 Clip::wrap(f32 t) const {
    if (length <= 0.0f) return 0.0f;
    if (loop) {
        f32 w = std::fmod(t, length);
        if (w < 0.0f) w += length;
        return w;
    }
    return t < 0.0f ? 0.0f : (t > length ? length : t);
}

void Clip::sample(const Skeleton& sk, f32 time, Pose& out) const {
    const f32 t = wrap(time);
    out.resize(sk.count());
    for (std::size_t i = 0; i < sk.count(); ++i) {
        const Bone& b = sk.bone(static_cast<i32>(i));
        out.translations[i] = b.bind_translation;
        out.rotations[i] = b.bind_rotation;
        out.scales[i] = b.bind_scale;
    }
    for (const auto& tr : tracks) {
        if (tr.bone < 0 || tr.bone >= static_cast<i32>(sk.count())) continue;
        const std::size_t i = static_cast<std::size_t>(tr.bone);
        tr.sample(t, out.translations[i], out.rotations[i], out.scales[i]);
    }
}

std::vector<std::string> Clip::events_between(f32 from, f32 to) const {
    std::vector<std::string> out;
    if (loop && length > 0.0f) {
        f32 f = std::fmod(from, length);
        if (f < 0.0f) f += length;
        f32 t = std::fmod(to, length);
        if (t < 0.0f) t += length;
        const bool wrapped = t <= f;
        for (const auto& e : events) {
            if (wrapped ? (e.time > f || e.time <= t) : (e.time > f && e.time <= t)) out.push_back(e.name);
        }
        return out;
    }
    for (const auto& e : events)
        if (e.time > from && e.time <= to) out.push_back(e.name);
    return out;
}

// =============================================================== animator ==
Animator::Animator(const Skeleton& sk) : sk_(sk) { pose_ = Pose::identity(sk.count()); }

void Animator::play(const Clip& clip, f32 fade_in) {
    from_ = current_;
    from_time_ = time_;
    current_ = &clip;
    time_ = 0;
    fade_ = 0;
    fade_t_ = fade_in;
}

void Animator::crossfade(const Clip& clip, f32 duration) { play(clip, duration); }

std::vector<std::string> Animator::update(f32 dt) {
    std::vector<std::string> fired;
    if (!current_) return fired;
    const f32 prev = time_;
    time_ += dt * speed_;

    Pose target;
    current_->sample(sk_, time_, target);
    if (from_ && fade_t_ > 0.0f) {
        fade_ += dt / fade_t_;
        Pose from_pose;
        from_->sample(sk_, from_time_ + dt * speed_, from_pose);
        blend_pose(from_pose, target, fade_ >= 1.0f ? 1.0f : fade_, pose_);
        if (fade_ >= 1.0f) from_ = nullptr;
    } else {
        pose_ = target;
        from_ = nullptr;
    }
    fired = current_->events_between(prev, time_);
    return fired;
}

// ===================================================================== IK ==
TwoBoneResult solve_two_bone(math::Vec3 root, math::Vec3 target, math::Vec3 pole, f32 l1, f32 l2) {
    TwoBoneResult res;
    const math::Vec3 to_target = target - root;
    const f32 d = to_target.length();
    const f32 maxd = l1 + l2;
    const f32 mind = std::fabs(l1 - l2);
    res.reachable = d <= maxd && d >= mind && d > 1e-6f;
    const f32 dc = d < mind + 1e-4f ? mind + 1e-4f : (d > maxd - 1e-4f ? maxd - 1e-4f : d);
    const math::Vec3 dir = d > 1e-6f ? to_target / d : math::Vec3(0, 1, 0);

    f32 cos_a = (l1 * l1 + dc * dc - l2 * l2) / (2.0f * l1 * dc);
    cos_a = cos_a < -1.0f ? -1.0f : (cos_a > 1.0f ? 1.0f : cos_a);
    const f32 angle = std::acos(cos_a);

    // Build an in-plane basis oriented toward the pole hint.
    math::Vec3 side = pole - root;
    math::Vec3 perp = side - dir * side.dot(dir);
    if (perp.length() < 1e-5f) {
        math::Vec3 any = std::fabs(dir.y) < 0.9f ? math::Vec3(0, 1, 0) : math::Vec3(1, 0, 0);
        perp = any - dir * any.dot(dir);
    }
    const math::Vec3 bend = perp.normalized();

    res.elbow = root + (dir * std::cos(angle) + bend * std::sin(angle)) * l1;
    res.end = res.reachable ? target : root + dir * maxd;
    return res;
}

void apply_two_bone(const Skeleton& sk, Pose& pose, i32 root, i32 child, i32 end,
                    math::Vec3 target, math::Vec3 pole) {
    if (root < 0 || child < 0 || end < 0) return;
    const std::vector<math::Vec3> pos = pose.joint_positions(sk);
    const f32 l1 = sk.bone_length(child);
    const f32 l2 = sk.bone_length(end);
    const TwoBoneResult s = solve_two_bone(pos[static_cast<std::size_t>(root)], target, pole, l1, l2);

    // Bones must start from their bind orientation for the from-to remap to be
    // exact; that is the canonical setup for an IK rig.
    const math::Vec3 rest0 = (sk.rest_positions()[static_cast<std::size_t>(child)] -
                              sk.rest_positions()[static_cast<std::size_t>(root)]);
    const math::Vec3 rest1 = (sk.rest_positions()[static_cast<std::size_t>(end)] -
                              sk.rest_positions()[static_cast<std::size_t>(child)]);
    const math::Vec3 d0 = (s.elbow - pos[static_cast<std::size_t>(root)]).normalized();
    const math::Vec3 d1 = (s.end - s.elbow).normalized();
    const math::Quat w0 = quat_from_to(rest0.normalized(), d0);
    const math::Quat w1 = quat_from_to(rest1.normalized(), d1);
    pose.rotations[static_cast<std::size_t>(root)] = w0;
    pose.rotations[static_cast<std::size_t>(child)] = w0.conjugate() * w1;
}

} // namespace prism::anim
