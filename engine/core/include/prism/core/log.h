// PRISM ENGINE — core/log.h : leveled logger, offline ring buffer + optional file sink.
#pragma once
#include <cstdio>
#include <mutex>
#include <string>
#include <deque>
#include <functional>
#include "../core/types.h"

namespace prism {

enum class LogLevel : u8 { Trace, Debug, Info, Warn, Error, Fatal };

inline const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

/// Offline-only logger. No network sink exists by design (see docs/11).
class Log {
public:
    static Log& get() { static Log l; return l; }

    void set_level(LogLevel l) { level_ = l; }
    [[nodiscard]] LogLevel level() const { return level_; }
    void set_file(const char* path);           // crash-log capture, local file only
    void add_sink(std::function<void(LogLevel, Str)> sink);

    void write(LogLevel l, Str tag, Str msg);
    [[nodiscard]] std::vector<std::string> drain();   // for the editor / ADB logcat bridge

    template <typename... Args>
    void fmt(LogLevel l, Str tag, Str format, Args&&... args) {
        char buf[1024];
        std::snprintf(buf, sizeof(buf), std::string(format).c_str(), std::forward<Args>(args)...);
        write(l, tag, buf);
    }

private:
    Log() = default;
    LogLevel level_ = LogLevel::Info;
    std::mutex mu_;
    std::deque<std::string> ring_;
    std::vector<std::function<void(LogLevel, Str)>> sinks_;
    std::FILE* file_ = nullptr;
};

#define PRISM_LOG(lvl, tag, msg) ::prism::Log::get().write(lvl, tag, msg)
#define PRISM_INFO(tag, msg)  PRISM_LOG(::prism::LogLevel::Info,  tag, msg)
#define PRISM_WARN(tag, msg)  PRISM_LOG(::prism::LogLevel::Warn,  tag, msg)
#define PRISM_ERROR(tag, msg) PRISM_LOG(::prism::LogLevel::Error, tag, msg)
#define PRISM_TRACE(tag, msg) PRISM_LOG(::prism::LogLevel::Trace, tag, msg)

} // namespace prism
