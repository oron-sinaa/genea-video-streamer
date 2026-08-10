#include "streamer/Config.h"
#include "streamer/HlsMuxer.h"
#include "streamer/HttpServer.h"
#include "streamer/Logger.h"
#include "streamer/PacketClock.h"
#include "streamer/PipelineHealth.h"
#include "streamer/RtspSource.h"
#include "streamer/StreamCopyPlanner.h"
#include "streamer/StreamManager.h"
#include "streamer/StreamWorker.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

namespace {

// Set from a signal handler; only safe operation is a lock-free store/load.
std::atomic<bool> g_shutdownRequested{false};

void handleShutdownSignal(int /*signal*/) {
    g_shutdownRequested.store(true);
}

void terminateHandler() {
    LOG_ERROR("FATAL: std::terminate() called!");
    if (std::exception_ptr ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            LOG_ERROR("  Exception: %s", e.what());
        } catch (...) {
            LOG_ERROR("  Unknown exception type");
        }
    } else {
        LOG_ERROR("  No active exception");
    }
    std::abort();
}

}  // namespace

// Multi-stream mode (Phase 6)
int runMultiStreamMode(const streamer::AppConfig& config) {
    LOG_INFO("Running in multi-stream mode with %zu stream(s)", config.streams.size());

    // Create stream manager
    streamer::StreamManager manager(config);

    // Start all streams (continue even if some/all fail - HTTP server still useful for health/debug)
    int started = manager.start();
    if (started == 0) {
        LOG_WARN("No streams started; HTTP server will be available for health checks and debugging");
    } else {
        LOG_INFO("Started %d stream(s); waiting for shutdown signal...", started);
    }

    // Start HTTP server (regardless of stream startup status)
    // Users can access /api/health to see why streams failed, and /api/streams for status
    streamer::HttpServer::ServerConfig http_config;
    http_config.listen_port = config.http.listen_port;
    http_config.listen_address = "0.0.0.0";
    http_config.enable_cors = true;
    
    // Set database path from environment or use default
    const char* db_path_env = std::getenv("DETECTION_DB_PATH");
    if (db_path_env) {
        http_config.database_path = db_path_env;
        LOG_INFO("Using detection database path from env: %s", db_path_env);
    } else {
        LOG_INFO("Using default detection database path: %s", http_config.database_path.c_str());
    }

    streamer::HttpServer http_server(&manager, http_config);
    if (!http_server.start()) {
        LOG_ERROR("Failed to start HTTP server: %s", http_server.getLastError().c_str());
        if (started > 0) {
            manager.stop();
        }
        return EXIT_FAILURE;
    }

    LOG_INFO("HTTP server started on 0.0.0.0:%u", config.http.listen_port);

    // Spawn AI inference worker (Python subprocess)
    // NOTE: Detection worker reads HLS segments from disk and writes to shared database
    // It runs independently and communicates via SQLite database
    // The HTTP server provides query API for detection results
    LOG_INFO("Starting AI detection worker (Python subprocess)...");
    // TODO: Implement Python subprocess spawning here
    // For now, run detection_worker.py separately in container:
    //   python3 -m ai_inference.detection_worker --config config/inference.yaml

    // If no streams started, at least provide useful feedback before waiting
    if (started == 0) {
        LOG_WARN("Streams failed to start. You can check:");
        LOG_WARN("  - Health API: curl http://localhost:%u/api/health", config.http.listen_port);
        LOG_WARN("  - Streams: curl http://localhost:%u/api/streams", config.http.listen_port);
        LOG_WARN("Waiting for shutdown signal (Ctrl+C)...");
    }

    // Monitor streams until shutdown signal
    const long statusInterval = 500;  // ms
    auto lastStatusTime = std::chrono::system_clock::now();
    bool allStreamsFailedReported = false;

    while (!g_shutdownRequested.load()) {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatusTime).count();

        if (elapsed >= statusInterval) {
            auto aggregate = manager.getAggregateHealth();
            LOG_INFO(
                "Multi-stream status: %u active, %u error, total_reconnects=%u, "
                "packets_written=%llu",
                aggregate.active_streams,
                aggregate.error_streams,
                aggregate.total_reconnects,
                static_cast<unsigned long long>(aggregate.total_packets_written));
            lastStatusTime = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Report when all streams have failed (only once), but don't exit
        // Keep HTTP server running for diagnostics
        if (!allStreamsFailedReported && manager.getActiveStreamCount() == 0 && started > 0) {
            LOG_WARN("All streams have failed or stopped; HTTP server still available for diagnostics");
            allStreamsFailedReported = true;
        }
    }

    LOG_INFO("Shutting down HTTP server...");
    http_server.stop();

    LOG_INFO("Shutting down all streams...");
    // StreamManager destructor will also call stop(), but we make stop() idempotent
    // by checking if any workers are still active before attempting shutdown
    manager.stop();

    // Print final aggregate health
    auto finalHealth = manager.getAggregateHealth();
    LOG_INFO(
        "Final stats: total_packets_written=%llu, total_reconnects=%u, error_streams=%u",
        static_cast<unsigned long long>(finalHealth.total_packets_written),
        finalHealth.total_reconnects,
        finalHealth.error_streams);

    // StreamManager and HttpServer will be destroyed here; both have idempotent destructors
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    const std::string configPath = (argc > 1) ? argv[1] : "config/rtsp-ingest.yaml";

    // Install custom terminate handler for debugging
    std::set_terminate(terminateHandler);

    try {
        streamer::AppConfig config;
        try {
            config = streamer::loadConfig(configPath);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load configuration from '%s': %s", configPath.c_str(), e.what());
            return EXIT_FAILURE;
        }

        // Validate configuration: at least one stream must be configured
        if (config.streams.empty() && config.rtsp.url.empty()) {
            LOG_ERROR("No streams configured. Provide either:");
            LOG_ERROR("  - Legacy mode: rtsp.url in config");
            LOG_ERROR("  - Multi-stream mode: streams array in config");
            return EXIT_FAILURE;
        }

        std::signal(SIGINT, handleShutdownSignal);
        std::signal(SIGTERM, handleShutdownSignal);

        avformat_network_init();

        int result;
        
        try {
            result = runMultiStreamMode(config);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during streaming: %s", e.what());
            result = EXIT_FAILURE;
        }

        avformat_network_deinit();
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal exception in main: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_ERROR("Fatal unknown exception in main");
        return EXIT_FAILURE;
    }
}
