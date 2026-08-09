#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace streamer {

// Individual stream configuration (Phase 6: Multi-stream support)
struct StreamConfig {
    std::string name;           // Stream identifier (e.g., "camera-1")
    std::string rtsp_url;       // RTSP source URL
    std::string hls_output;     // Per-stream HLS output directory (e.g., "segments/camera1/")
    
    // Reconnect policy per stream
    struct {
        bool enabled = true;
        uint32_t initial_delay_ms = 1000;
        uint32_t max_delay_ms = 30000;
        uint32_t jitter_percent = 15;
        uint32_t stale_timeout_s = 10;
    } reconnect;
};

// Configuration for RTSP capture source.
struct RtspConfig {
    std::string url;
    std::string transport = "tcp";  // "tcp" or "udp"
    int timeout_us = 5000000;       // connection/read timeout in microseconds
    
    // Reconnection policy settings
    bool reconnect_enabled = true;              // enable automatic reconnection on disconnect
    uint32_t reconnect_initial_delay_ms = 1000; // initial backoff delay (milliseconds)
    uint32_t reconnect_max_delay_ms = 30000;    // maximum backoff delay (milliseconds)
    uint32_t reconnect_jitter_percent = 15;     // jitter variance (±percent)
    uint32_t stale_timeout_s = 10;              // detect stale source if no packets for N seconds
};

// Configuration for HLS muxing and segment output.
struct HlsConfig {
    std::string output_dir = "segments";       // directory for .ts files and playlists
    int segment_duration_s = 3;                // target segment length in seconds
    int archive_retention_hours = 2;           // keep this many hours of archived segments
    int cleanup_interval_s = 300;              // run cleanup scan every N seconds
    bool enable_discontinuity_markers = true;  // write #EXT-X-DISCONTINUITY after reconnect
};

// HTTP server configuration
struct HttpConfig {
    uint16_t listen_port = 8000;
};

// Resource limits configuration (Phase 6)
struct ResourceConfig {
    uint32_t max_streams = 10;
    float max_cpu_per_stream = 1.0f;        // CPU cores
    uint32_t max_memory_per_stream = 512;   // MB
};

// Top-level application configuration.
struct AppConfig {
    // Legacy single-stream mode (Phase 0-5 compatibility)
    RtspConfig rtsp;
    HlsConfig hls;
    
    // Phase 6: Multi-stream support
    std::vector<StreamConfig> streams;      // Array of stream configs
    HttpConfig http;                        // HTTP server settings
    ResourceConfig resources;               // Resource limits
    
    // Helper: Returns true if using multi-stream mode
    bool isMultiStream() const {
        return !streams.empty();
    }
};

// Loads and validates configuration from a YAML file at `path`.
// Supports both:
//  - Legacy single-stream format (rtsp.url + hls settings)
//  - New multi-stream format (streams[] array)
// Throws std::runtime_error if file is missing, malformed, or validation fails.
AppConfig loadConfig(const std::string& path);

}  // namespace streamer
