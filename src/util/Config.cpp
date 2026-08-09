#include "streamer/Config.h"

#include <yaml-cpp/yaml.h>

#include <stdexcept>

namespace streamer {

AppConfig loadConfig(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::BadFile&) {
        throw std::runtime_error("Config file not found or unreadable: " + path);
    } catch (const YAML::ParserException& e) {
        throw std::runtime_error("Failed to parse config file '" + path + "': " + e.what());
    }

    if (!root.IsMap() || !root["rtsp"]) {
        throw std::runtime_error("Config file '" + path + "' is missing required 'rtsp' section");
    }

    const YAML::Node rtspNode = root["rtsp"];

    if (!rtspNode["url"] || rtspNode["url"].as<std::string>().empty()) {
        throw std::runtime_error("Config file '" + path + "' is missing required 'rtsp.url' field");
    }

    AppConfig config;
    config.rtsp.url = rtspNode["url"].as<std::string>();
    config.rtsp.transport = rtspNode["transport"].as<std::string>("tcp");
    config.rtsp.timeout_us = rtspNode["timeout_us"].as<int>(5000000);

    if (config.rtsp.transport != "tcp" && config.rtsp.transport != "udp") {
        throw std::runtime_error(
            "Config file '" + path + "' has invalid 'rtsp.transport' value '" +
            config.rtsp.transport + "' (expected 'tcp' or 'udp')");
    }

    if (config.rtsp.timeout_us <= 0) {
        throw std::runtime_error(
            "Config file '" + path + "' has invalid 'rtsp.timeout_us' value (must be positive)");
    }

    // Parse reconnection policy config (optional section; defaults already in struct).
    if (rtspNode["reconnect"]) {
        const YAML::Node reconnectNode = rtspNode["reconnect"];
        config.rtsp.reconnect_enabled = reconnectNode["enabled"].as<bool>(true);
        config.rtsp.reconnect_initial_delay_ms = reconnectNode["initial_delay_ms"].as<uint32_t>(1000);
        config.rtsp.reconnect_max_delay_ms = reconnectNode["max_delay_ms"].as<uint32_t>(30000);
        config.rtsp.reconnect_jitter_percent = reconnectNode["jitter_percent"].as<uint32_t>(15);
        config.rtsp.stale_timeout_s = reconnectNode["stale_timeout_s"].as<uint32_t>(10);

        if (config.rtsp.reconnect_initial_delay_ms <= 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'rtsp.reconnect.initial_delay_ms' value (must be positive)");
        }
        if (config.rtsp.reconnect_max_delay_ms < config.rtsp.reconnect_initial_delay_ms) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'rtsp.reconnect.max_delay_ms' (must be >= initial_delay_ms)");
        }
        if (config.rtsp.reconnect_jitter_percent > 100) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'rtsp.reconnect.jitter_percent' value (must be <= 100)");
        }
        if (config.rtsp.stale_timeout_s <= 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'rtsp.reconnect.stale_timeout_s' value (must be positive)");
        }
    }

    // Parse HLS config (optional section; defaults are already set in struct).
    if (root["hls"]) {
        const YAML::Node hlsNode = root["hls"];
        config.hls.output_dir = hlsNode["output_dir"].as<std::string>(config.hls.output_dir);
        config.hls.segment_duration_s = hlsNode["segment_duration_s"].as<int>(config.hls.segment_duration_s);
        config.hls.archive_retention_hours = hlsNode["archive_retention_hours"].as<int>(config.hls.archive_retention_hours);
        config.hls.cleanup_interval_s = hlsNode["cleanup_interval_s"].as<int>(config.hls.cleanup_interval_s);
        config.hls.enable_discontinuity_markers = hlsNode["enable_discontinuity_markers"].as<bool>(true);

        if (config.hls.segment_duration_s <= 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'hls.segment_duration_s' value (must be positive)");
        }
        if (config.hls.archive_retention_hours <= 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'hls.archive_retention_hours' value (must be positive)");
        }
        if (config.hls.cleanup_interval_s <= 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'hls.cleanup_interval_s' value (must be positive)");
        }
    }

    return config;
}

}  // namespace streamer
