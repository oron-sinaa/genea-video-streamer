#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace streamer {

// Returns the current local time formatted as "YYYY-MM-DD HH:MM:SS".
inline std::string logTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace streamer

// Minimal leveled logging. Not thread-safe by design (single-pipeline MVP);
// revisit if/when multiple pipelines log concurrently (see design.md Phase 6).
#define LOG_INFO(fmt, ...) \
    std::fprintf(stdout, "[%s] [INFO] " fmt "\n", streamer::logTimestamp().c_str(), ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    std::fprintf(stderr, "[%s] [WARN] " fmt "\n", streamer::logTimestamp().c_str(), ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    std::fprintf(stderr, "[%s] [ERROR] " fmt "\n", streamer::logTimestamp().c_str(), ##__VA_ARGS__)
