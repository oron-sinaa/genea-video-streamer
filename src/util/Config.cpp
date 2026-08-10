#include "streamer/Config.h"
#include "streamer/Logger.h"

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

    if (!root.IsMap()) {
        throw std::runtime_error("Config file '" + path + "' root must be a map");
    }

    AppConfig config;

    // Check for multi-stream mode (Phase 6)
    if (root["streams"] && root["streams"].IsSequence() && root["streams"].size() > 0) {
        // Multi-stream mode
        const YAML::Node streamsNode = root["streams"];
        
        for (size_t i = 0; i < streamsNode.size(); ++i) {
            const YAML::Node streamNode = streamsNode[i];
            
            if (!streamNode.IsMap()) {
                throw std::runtime_error("Config file '" + path + "' streams[" + std::to_string(i) + "] must be a map");
            }
            
            if (!streamNode["name"] || streamNode["name"].as<std::string>().empty()) {
                throw std::runtime_error(
                    "Config file '" + path + "' streams[" + std::to_string(i) + "] is missing required 'name' field");
            }
            
            if (!streamNode["rtsp_url"] || streamNode["rtsp_url"].as<std::string>().empty()) {
                throw std::runtime_error(
                    "Config file '" + path + "' streams[" + std::to_string(i) + "] is missing required 'rtsp_url' field");
            }
            
            StreamConfig streamConfig;
            streamConfig.name = streamNode["name"].as<std::string>();
            streamConfig.rtsp_url = streamNode["rtsp_url"].as<std::string>();
            streamConfig.hls_output = streamNode["hls_output"].as<std::string>(
                "segments/" + streamConfig.name + "/");
            
            // Parse per-stream reconnect policy (optional)
            if (streamNode["reconnect"]) {
                const YAML::Node reconnectNode = streamNode["reconnect"];
                streamConfig.reconnect.enabled = reconnectNode["enabled"].as<bool>(true);
                streamConfig.reconnect.initial_delay_ms = reconnectNode["initial_delay_ms"].as<uint32_t>(1000);
                streamConfig.reconnect.max_delay_ms = reconnectNode["max_delay_ms"].as<uint32_t>(30000);
                streamConfig.reconnect.jitter_percent = reconnectNode["jitter_percent"].as<uint32_t>(15);
                streamConfig.reconnect.stale_timeout_s = reconnectNode["stale_timeout_s"].as<uint32_t>(10);
                
                // Validate reconnect settings
                if (streamConfig.reconnect.initial_delay_ms <= 0) {
                    throw std::runtime_error(
                        "Config file '" + path + "' streams[" + std::to_string(i) +
                        "].reconnect.initial_delay_ms must be positive");
                }
                if (streamConfig.reconnect.max_delay_ms < streamConfig.reconnect.initial_delay_ms) {
                    throw std::runtime_error(
                        "Config file '" + path + "' streams[" + std::to_string(i) +
                        "].reconnect.max_delay_ms must be >= initial_delay_ms");
                }
                if (streamConfig.reconnect.jitter_percent > 100) {
                    throw std::runtime_error(
                        "Config file '" + path + "' streams[" + std::to_string(i) +
                        "].reconnect.jitter_percent must be <= 100");
                }
                if (streamConfig.reconnect.stale_timeout_s <= 0) {
                    throw std::runtime_error(
                        "Config file '" + path + "' streams[" + std::to_string(i) +
                        "].reconnect.stale_timeout_s must be positive");
                }
            }
            
            config.streams.push_back(streamConfig);
        }
        
        LOG_INFO("Loaded %zu stream(s) from config", config.streams.size());
    } else if (root["rtsp"]) {
        // Legacy single-stream mode (backward compatibility)
        const YAML::Node rtspNode = root["rtsp"];

        if (!rtspNode["url"] || rtspNode["url"].as<std::string>().empty()) {
            throw std::runtime_error("Config file '" + path + "' is missing required 'rtsp.url' field");
        }

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
        
        LOG_INFO("Loaded single-stream config (legacy mode)");
    } else {
        throw std::runtime_error("Config file '" + path + "' must have either 'rtsp' or 'streams' section");
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
    
    // Parse HTTP config (optional, Phase 6)
    if (root["http"]) {
        const YAML::Node httpNode = root["http"];
        config.http.listen_port = httpNode["listen_port"].as<uint16_t>(8000);
        
        if (config.http.listen_port == 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'http.listen_port' value (must be non-zero)");
        }
    }
    
    // Parse resource config (optional, Phase 6)
    if (root["resources"]) {
        const YAML::Node resourceNode = root["resources"];
        config.resources.max_streams = resourceNode["max_streams"].as<uint32_t>(10);
        config.resources.max_cpu_per_stream = resourceNode["max_cpu_per_stream"].as<float>(1.0f);
        config.resources.max_memory_per_stream = resourceNode["max_memory_per_stream"].as<uint32_t>(512);
        
        if (config.resources.max_streams == 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'resources.max_streams' value (must be positive)");
        }
        if (config.resources.max_cpu_per_stream <= 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'resources.max_cpu_per_stream' value (must be positive)");
        }
        if (config.resources.max_memory_per_stream == 0) {
            throw std::runtime_error(
                "Config file '" + path + "' has invalid 'resources.max_memory_per_stream' value (must be positive)");
        }
    }

    // Parse playback config (optional, latency/buffering settings for client)
    if (root["playback"]) {
        const YAML::Node playbackNode = root["playback"];
        
        if (playbackNode["live_mode"]) {
            const YAML::Node liveModeNode = playbackNode["live_mode"];
            config.playback.live_mode.back_buffer_length_s = 
                liveModeNode["back_buffer_length_s"].as<int>(10);
            config.playback.live_mode.sync_segment_count = 
                liveModeNode["sync_segment_count"].as<int>(2);
            config.playback.live_mode.max_buffer_length_s = 
                liveModeNode["max_buffer_length_s"].as<int>(30);
            config.playback.live_mode.max_buffer_length_absolute_s = 
                liveModeNode["max_buffer_length_absolute_s"].as<int>(60);
            
            // Validate playback settings
            if (config.playback.live_mode.back_buffer_length_s <= 0) {
                throw std::runtime_error(
                    "Config file '" + path + "' has invalid 'playback.live_mode.back_buffer_length_s' value (must be positive)");
            }
            if (config.playback.live_mode.sync_segment_count <= 0) {
                throw std::runtime_error(
                    "Config file '" + path + "' has invalid 'playback.live_mode.sync_segment_count' value (must be positive)");
            }
            if (config.playback.live_mode.max_buffer_length_s <= 0) {
                throw std::runtime_error(
                    "Config file '" + path + "' has invalid 'playback.live_mode.max_buffer_length_s' value (must be positive)");
            }
            if (config.playback.live_mode.max_buffer_length_absolute_s <= 0) {
                throw std::runtime_error(
                    "Config file '" + path + "' has invalid 'playback.live_mode.max_buffer_length_absolute_s' value (must be positive)");
            }
        }
    }

    return config;
}

}  // namespace streamer
