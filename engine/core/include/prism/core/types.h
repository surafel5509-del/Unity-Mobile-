// =====================================================================
//  PRISM ENGINE — "Every Angle. Every World. One File."
//  core/types.h — foundation types, ids, platform macros
//  License: MIT
// =====================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <limits>

#if defined(_WIN32)
  #define PRISM_API __declspec(dllexport)
#else
  #define PRISM_API __attribute__((visibility("default")))
#endif

#if defined(__ANDROID__)
  #define PRISM_PLATFORM_ANDROID 1
#elif defined(__linux__)
  #define PRISM_PLATFORM_LINUX 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
  #define PRISM_ARCH_ARM64 1
#elif defined(__arm__)
  #define PRISM_ARCH_ARM32 1
#elif defined(__x86_64__)
  #define PRISM_ARCH_X64 1
#endif

namespace prism {

using u8  = std::uint8_t;   using i8  = std::int8_t;
using u16 = std::uint16_t;  using i16 = std::int16_t;
using u32 = std::uint32_t;  using i32 = std::int32_t;
using u64 = std::uint64_t;  using i64 = std::int64_t;
using f32 = float;          using f64 = double;

inline constexpr u32 kInvalidId = 0xFFFFFFFFu;
inline constexpr f32 kEpsilon   = 1e-6f;

/// Strong, generation-checked handle for ECS entities.
struct EntityId {
    u32 index      = kInvalidId;
    u32 generation = 0;
    [[nodiscard]] bool valid() const { return index != kInvalidId; }
    bool operator==(const EntityId& o) const { return index == o.index && generation == o.generation; }
};

/// Non-owning string view alias used across the engine boundary (JNI safe).
using Str = std::string_view;

/// Engine version — bumped by tools/bump_version.py, mirrored into Android app.
struct Version {
    i32 major = 1, minor = 0, patch = 0;
    [[nodiscard]] std::string str() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }
};

inline constexpr Version kEngineVersion{1, 0, 0};

/// Every subsystem reports one of these from Module::on_tick.
enum class TickStatus : u8 { Ok, Paused, Degraded, Failed };

} // namespace prism
