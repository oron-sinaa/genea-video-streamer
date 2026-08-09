#pragma once

#include <string>

namespace streamer {

// Configuration for RTSP capture source.
struct RtspConfig {
    std::string url;
    std::string transport = "tcp";  // "tcp" or "udp"
    int timeout_us = 5000000;       // connection/read timeout in microseconds
};

// Configuration for HLS muxing and segment output.
struct HlsConfig {
    std::string output_dir = "segments";       // directory for .ts files and playlists
    int segment_duration_s = 3;                // target segment length in seconds
    int archive_retention_hours = 2;           // keep this many hours of archived segments
    int cleanup_interval_s = 300;              // run cleanup scan every N seconds
};

// Top-level application configuration. Extended as new modules are added.
struct AppConfig {
    RtspConfig rtsp;
    HlsConfig hls;
};

// Loads and validates configuration from a YAML file at `path`.
// Throws std::runtime_error if the file is missing, malformed, or missing
// required fields (currently: rtsp.url).
AppConfig loadConfig(const std::string& path);

}  // namespace streamer
