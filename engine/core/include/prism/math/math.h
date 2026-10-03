// PRISM ENGINE — math/math.h : vector, matrix, quaternion math (header-only, SIMD friendly)
#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include "../core/types.h"

namespace prism::math {

inline constexpr f32 Pi      = 3.14159265358979323846f;
inline constexpr f32 TwoPi   = Pi * 2.0f;
inline constexpr f32 HalfPi  = Pi * 0.5f;
inline constexpr f32 Deg2Rad = Pi / 180.0f;
inline constexpr f32 Rad2Deg = 180.0f / Pi;

inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline f32 lerpf(f32 a, f32 b, f32 t)    { return a + (b - a) * t; }
inline f32 smoothstep(f32 e0, f32 e1, f32 x) {
    f32 t = clampf((x - e0) / (e1 - e0 + kEpsilon), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline f32 approach(f32 cur, f32 target, f32 maxDelta) {
    if (std::fabs(target - cur) <= maxDelta) return target;
    return cur + (target > cur ? maxDelta : -maxDelta);
}

struct Vec2 {
    f32 x = 0, y = 0;
    Vec2() = default;
    Vec2(f32 xx, f32 yy) : x(xx), y(yy) {}
    Vec2  operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2  operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2  operator-()       const { return {-x, -y}; }
    Vec2  operator*(f32 s)  const { return {x * s, y * s}; }
    Vec2  operator/(f32 s)  const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(f32 s)  { x *= s; y *= s; return *this; }
    bool  operator==(Vec2 o) const { return std::fabs(x - o.x) < kEpsilon && std::fabs(y - o.y) < kEpsilon; }
    f32   dot(Vec2 o)   const { return x * o.x + y * o.y; }
    f32   cross(Vec2 o) const { return x * o.y - y * o.x; }   // z of 3D cross
    f32   length_sq() const { return dot(*this); }
    f32   length()    const { return std::sqrt(length_sq()); }
    f32   distance(Vec2 o) const { return (*this - o).length(); }
    Vec2  normalized() const { f32 l = length(); return l > kEpsilon ? Vec2{x / l, y / l} : Vec2{0, 0}; }
    Vec2  perpendicular() const { return {-y, x}; }
    Vec2  lerp(Vec2 o, f32 t) const { return {lerpf(x, o.x, t), lerpf(y, o.y, t)}; }
    f32&  operator[](int i) { return i == 0 ? x : y; }
    f32   operator[](int i) const { return i == 0 ? x : y; }
};
inline Vec2 operator*(f32 s, Vec2 v) { return v * s; }
inline Vec2 min(Vec2 a, Vec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline Vec2 max(Vec2 a, Vec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }

struct Vec3 {
    f32 x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(f32 xx, f32 yy, f32 zz) : x(xx), y(yy), z(zz) {}
    explicit Vec3(Vec2 v, f32 zz = 0) : x(v.x), y(v.y), z(zz) {}
    Vec3  operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3  operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3  operator-()       const { return {-x, -y, -z}; }
    Vec3  operator*(f32 s)  const { return {x * s, y * s, z * s}; }
    Vec3  operator*(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3  operator/(f32 s)  const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(f32 s)  { x *= s; y *= s; z *= s; return *this; }
    bool  operator==(Vec3 o) const {
        return std::fabs(x - o.x) < kEpsilon && std::fabs(y - o.y) < kEpsilon && std::fabs(z - o.z) < kEpsilon;
    }
    f32   dot(Vec3 o)   const { return x * o.x + y * o.y + z * o.z; }
    Vec3  cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    f32   length_sq() const { return dot(*this); }
    f32   length()    const { return std::sqrt(length_sq()); }
    f32   distance(Vec3 o) const { return (*this - o).length(); }
    Vec3  normalized() const { f32 l = length(); return l > kEpsilon ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0}; }
    Vec3  lerp(Vec3 o, f32 t) const { return {lerpf(x, o.x, t), lerpf(y, o.y, t), lerpf(z, o.z, t)}; }
    Vec3  reflect(Vec3 n) const { return *this - n * (2.0f * dot(n)); }
    Vec2  xy() const { return {x, y}; }
    f32&  operator[](int i) { return (&x)[i]; }
    f32   operator[](int i) const { return (&x)[i]; }
    static Vec3 up()    { return {0, 1, 0}; }
    static Vec3 right() { return {1, 0, 0}; }
    static Vec3 fwd()   { return {0, 0, -1}; }
};
inline Vec3 operator*(f32 s, Vec3 v) { return v * s; }

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(f32 xx, f32 yy, f32 zz, f32 ww) : x(xx), y(yy), z(zz), w(ww) {}
    Vec4(Vec3 v, f32 ww) : x(v.x), y(v.y), z(v.z), w(ww) {}
    Vec4  operator+(Vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4  operator*(f32 s)  const { return {x * s, y * s, z * s, w * s}; }
    Vec4& operator+=(Vec4 o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
    f32   dot(Vec4 o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    Vec3  xyz() const { return {x, y, z}; }
    static Vec4 color(u8 r, u8 g, u8 b, u8 a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
};

/// Column-major 4x4 matrix, matches Vulkan / OpenGL ES clip conventions.
struct Mat4 {
    std::array<f32, 16> m{};
    Mat4() { identity(); }
    static Mat4 zeros() { Mat4 r; r.m.fill(0.0f); return r; }
    static Mat4 identity_matrix() { return Mat4{}; }
    void identity() { m.fill(0.0f); m[0] = m[5] = m[10] = m[15] = 1.0f; }
    f32& at(int row, int col)       { return m[col * 4 + row]; }
    f32  at(int row, int col) const { return m[col * 4 + row]; }
    const f32* data() const { return m.data(); }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r = zeros();
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                f32 sum = 0;
                for (int k = 0; k < 4; ++k) sum += at(rr, k) * o.at(k, c);
                r.at(rr, c) = sum;
            }
        return r;
    }
    Vec4 operator*(Vec4 v) const {
        return {at(0,0)*v.x + at(0,1)*v.y + at(0,2)*v.z + at(0,3)*v.w,
                at(1,0)*v.x + at(1,1)*v.y + at(1,2)*v.z + at(1,3)*v.w,
                at(2,0)*v.x + at(2,1)*v.y + at(2,2)*v.z + at(2,3)*v.w,
                at(3,0)*v.x + at(3,1)*v.y + at(3,2)*v.z + at(3,3)*v.w};
    }
    Vec3 transform_point(Vec3 p) const {
        Vec4 r = (*this) * Vec4(p, 1.0f);
        f32 w = std::fabs(r.w) > kEpsilon ? r.w : 1.0f;
        return {r.x / w, r.y / w, r.z / w};
    }
    Vec3 transform_dir(Vec3 d) const { return ((*this) * Vec4(d, 0.0f)).xyz(); }

    static Mat4 translate(Vec3 t) {
        Mat4 r; r.at(0,3) = t.x; r.at(1,3) = t.y; r.at(2,3) = t.z; return r;
    }
    static Mat4 scale(Vec3 s) { Mat4 r; r.at(0,0)=s.x; r.at(1,1)=s.y; r.at(2,2)=s.z; return r; }
    static Mat4 rotate_y(f32 rad) {
        Mat4 r; f32 c = std::cos(rad), s = std::sin(rad);
        r.at(0,0)= c; r.at(0,2)= s; r.at(2,0)= -s; r.at(2,2)= c; return r;
    }
    static Mat4 rotate_x(f32 rad) {
        Mat4 r; f32 c = std::cos(rad), s = std::sin(rad);
        r.at(1,1)= c; r.at(1,2)= -s; r.at(2,1)= s; r.at(2,2)= c; return r;
    }
    static Mat4 rotate_z(f32 rad) {
        Mat4 r; f32 c = std::cos(rad), s = std::sin(rad);
        r.at(0,0)= c; r.at(0,1)= -s; r.at(1,0)= s; r.at(1,1)= c; return r;
    }
    /// Right-handed perspective with [0,1] depth (Vulkan) or [-1,1] (GL ES via flag).
    static Mat4 perspective(f32 fovY_rad, f32 aspect, f32 zn, f32 zf, bool zeroToOneDepth = false) {
        Mat4 r = zeros();
        f32 f = 1.0f / std::tan(fovY_rad * 0.5f);
        r.at(0,0) = f / (aspect > kEpsilon ? aspect : 1.0f);
        r.at(1,1) = f;
        if (zeroToOneDepth) {
            r.at(2,2) = zf / (zn - zf);
            r.at(2,3) = (zn * zf) / (zn - zf);
        } else {
            r.at(2,2) = (zf + zn) / (zn - zf);
            r.at(2,3) = (2.0f * zf * zn) / (zn - zf);
        }
        r.at(3,2) = -1.0f;
        return r;
    }
    static Mat4 ortho(f32 l, f32 rr, f32 b, f32 t, f32 zn, f32 zf) {
        Mat4 r = zeros();
        r.at(0,0) = 2.0f / (rr - l); r.at(0,3) = -(rr + l) / (rr - l);
        r.at(1,1) = 2.0f / (t - b);  r.at(1,3) = -(t + b) / (t - b);
        r.at(2,2) = -2.0f / (zf - zn); r.at(2,3) = -(zf + zn) / (zf - zn);
        r.at(3,3) = 1.0f;
        return r;
    }
    static Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 r;
        r.at(0,0)= s.x; r.at(0,1)= s.y; r.at(0,2)= s.z; r.at(0,3)= -s.dot(eye);
        r.at(1,0)= u.x; r.at(1,1)= u.y; r.at(1,2)= u.z; r.at(1,3)= -u.dot(eye);
        r.at(2,0)=-f.x; r.at(2,1)=-f.y; r.at(2,2)=-f.z; r.at(2,3)=  f.dot(eye);
        return r;
    }
};

/// Quaternion, Hamilton product, w-first storage order (x,y,z,w accessors).
struct Quat {
    f32 x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(f32 xx, f32 yy, f32 zz, f32 ww) : x(xx), y(yy), z(zz), w(ww) {}
    static Quat from_axis_angle(Vec3 axis, f32 angle_rad) {
        Vec3 a = axis.normalized();
        f32 h = angle_rad * 0.5f, s = std::sin(h);
        return {a.x * s, a.y * s, a.z * s, std::cos(h)};
    }
    static Quat from_euler(f32 pitch, f32 yaw, f32 roll) {
        Quat qx = from_axis_angle({1,0,0}, pitch);
        Quat qy = from_axis_angle({0,1,0}, yaw);
        Quat qz = from_axis_angle({0,0,1}, roll);
        return qy * qx * qz;
    }
    Quat operator*(Quat o) const {
        return {w*o.x + x*o.w + y*o.z - z*o.y,
                w*o.y - x*o.z + y*o.w + z*o.x,
                w*o.z + x*o.y - y*o.x + z*o.w,
                w*o.w - x*o.x - y*o.y - z*o.z};
    }
    f32  norm_sq() const { return x*x + y*y + z*z + w*w; }
    Quat normalized() const {
        f32 n = std::sqrt(norm_sq());
        return n > kEpsilon ? Quat{x/n, y/n, z/n, w/n} : Quat{};
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Vec3 rotate(Vec3 v) const {
        Vec3 q{x, y, z};
        Vec3 t = 2.0f * q.cross(v);
        return v + t * w + q.cross(t);
    }
    Mat4 to_mat4() const {
        Mat4 r;
        f32 xx=x*x, yy=y*y, zz=z*z, xy=x*y, xz=x*z, yz=y*z, wx=w*x, wy=w*y, wz=w*z;
        r.at(0,0)=1-2*(yy+zz); r.at(0,1)=2*(xy-wz);   r.at(0,2)=2*(xz+wy);
        r.at(1,0)=2*(xy+wz);   r.at(1,1)=1-2*(xx+zz); r.at(1,2)=2*(yz-wx);
        r.at(2,0)=2*(xz-wy);   r.at(2,1)=2*(yz+wx);   r.at(2,2)=1-2*(xx+yy);
        return r;
    }
    static Quat slerp(Quat a, Quat b, f32 t) {
        f32 d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        Quat bb = b;
        if (d < 0.0f) { bb = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
        if (d > 0.9995f) {
            return Quat{a.x + (bb.x - a.x) * t, a.y + (bb.y - a.y) * t,
                        a.z + (bb.z - a.z) * t, a.w + (bb.w - a.w) * t}.normalized();
        }
        f32 th = std::acos(clampf(d, -1.0f, 1.0f));
        f32 s  = std::sin(th);
        f32 wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
        return Quat{a.x*wa + bb.x*wb, a.y*wa + bb.y*wb, a.z*wa + bb.z*wb, a.w*wa + bb.w*wb};
    }
};

struct Rect {
    f32 x = 0, y = 0, w = 0, h = 0;
    bool contains(Vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    bool intersects(Rect o) const {
        return !(o.x > x + w || o.x + o.w < x || o.y > y + h || o.y + o.h < y);
    }
    Rect intersection(Rect o) const {
        f32 nx = std::max(x, o.x), ny = std::max(y, o.y);
        f32 nw = std::min(x + w, o.x + o.w) - nx;
        f32 nh = std::min(y + h, o.y + o.h) - ny;
        return (nw > 0 && nh > 0) ? Rect{nx, ny, nw, nh} : Rect{0, 0, 0, 0};
    }
    f32 area() const { return w * h; }
};

struct Color {
    f32 r = 1, g = 1, b = 1, a = 1;
    u32  to_rgba8() const {
        auto q = [](f32 v) { return static_cast<u32>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return (q(r) << 24) | (q(g) << 16) | (q(b) << 8) | q(a);
    }
    static Color from_hex(u32 rgb) {
        return {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1.0f};
    }
    // Brand palette
    static Color prism_violet()   { return from_hex(0x7C3AED); }
    static Color spectrum_cyan()  { return from_hex(0x00D9FF); }
    static Color prism_gold()     { return from_hex(0xFFC93C); }
    static Color sunset_magenta() { return from_hex(0xFF3D9A); }
    static Color dark_bg()        { return from_hex(0x0A0A12); }
};

} // namespace prism::math
