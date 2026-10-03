#include "prism/core/log.h"
#include <chrono>
#include <cstdarg>

namespace prism {

void Log::set_file(const char* path) {
    std::lock_guard<std::mutex> g(mu_);
    if (file_) { std::fclose(file_); file_ = nullptr; }
    if (path && *path) file_ = std::fopen(path, "ab");
}

void Log::add_sink(std::function<void(LogLevel, Str)> sink) {
    std::lock_guard<std::mutex> g(mu_);
    if (sink) sinks_.push_back(std::move(sink));
}

void Log::write(LogLevel l, Str tag, Str msg) {
    if (static_cast<int>(l) < static_cast<int>(level_)) return;
    std::string line;
    line.reserve(tag.size() + msg.size() + 24);
    line += "[";
    line += level_name(l);
    line += "][";
    line += tag;
    line += "] ";
    line += msg;

    std::lock_guard<std::mutex> g(mu_);
    ring_.push_back(line);
    while (ring_.size() > 2048) ring_.pop_front();
    for (auto& s : sinks_) s(l, line);
    if (file_) {
        std::fprintf(file_, "%s\n", line.c_str());
        std::fflush(file_);
    }
#ifndef PRISM_QUIET
    std::fprintf(stderr, "%s\n", line.c_str());
#endif
}

std::vector<std::string> Log::drain() {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> out(ring_.begin(), ring_.end());
    return out;
}

} // namespace prism
