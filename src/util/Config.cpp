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

    return config;
}

}  // namespace streamer
