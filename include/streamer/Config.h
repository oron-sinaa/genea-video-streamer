#pragma once

#include <string>

namespace streamer {

// Configuration for the RTSP capture source.
struct RtspConfig {
    std::string url;
    std::string transport = "tcp";  // "tcp" or "udp"
    int timeout_us = 5000000;       // connection/read timeout in microseconds
};

// Top-level application configuration. Extended as new modules are added.
struct AppConfig {
    RtspConfig rtsp;
};

// Loads and validates configuration from a YAML file at `path`.
// Throws std::runtime_error if the file is missing, malformed, or missing
// required fields (currently: rtsp.url).
AppConfig loadConfig(const std::string& path);

}  // namespace streamer
