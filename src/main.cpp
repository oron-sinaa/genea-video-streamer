#include "streamer/Config.h"
#include "streamer/HlsMuxer.h"
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

}  // namespace

// Single-stream legacy mode (backward compatibility)
int runLegacyMode(const streamer::AppConfig& config) {
    LOG_INFO("Running in legacy single-stream mode");

    // Create pipeline health tracker (shared across components).
    streamer::PipelineHealth health;

    streamer::RtspSource source(config.rtsp, &health);
    if (!source.open()) {
        LOG_ERROR("Failed to open RTSP source; exiting");
        return EXIT_FAILURE;
    }

    // Initialize packet clock for timestamp normalization.
    // Output time base is 90000 (standard for HLS/MPEG-TS).
    AVStream* videoStream = source.videoStream();
    if (videoStream == nullptr) {
        LOG_ERROR("Failed to get video stream for clock initialization");
        source.close();
        return EXIT_FAILURE;
    }
    streamer::PacketClock clock(videoStream, 90000);
    LOG_INFO("Packet clock initialized for timestamp normalization (output_tb=90000)");

    // Validate source stream compatibility with MPEG-TS codec copy.
    const streamer::StreamCopyResult copyResult = streamer::StreamCopyPlanner::canCopyToMpegTs(videoStream);
    if (!copyResult.canCopy) {
        LOG_ERROR("Source stream is not compatible with MPEG-TS codec copy: %s", copyResult.reason.c_str());
        source.close();
        return EXIT_FAILURE;
    }
    LOG_INFO("Source stream validated for MPEG-TS codec copy");

    // Initialize HLS muxer.
    streamer::HlsMuxer muxer(config.hls);
    if (!muxer.open()) {
        LOG_ERROR("Failed to initialize HLS muxer; exiting");
        source.close();
        return EXIT_FAILURE;
    }

    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        LOG_ERROR("Failed to allocate AVPacket");
        muxer.close();
        source.close();
        return EXIT_FAILURE;
    }

    LOG_INFO("Starting packet read loop (Ctrl+C to stop)...");

    long videoPacketCount = 0;
    long otherPacketCount = 0;
    uint32_t lastReconnectCount = health.getReconnectCount();
    const long logInterval = 100;
    uint64_t totalBytesWritten = 0;

    while (!g_shutdownRequested.load()) {
        if (!source.readPacket(packet)) {
            LOG_WARN("Packet read failed or stream ended; stopping");
            break;
        }

        // Check if a reconnect just occurred and write discontinuity marker
        uint32_t currentReconnectCount = health.getReconnectCount();
        if (currentReconnectCount > lastReconnectCount && config.hls.enable_discontinuity_markers) {
            LOG_INFO("Reconnect detected, writing discontinuity marker to playlists");
            muxer.writeDiscontinuity();
            lastReconnectCount = currentReconnectCount;
        }

        if (packet->stream_index == source.videoStreamIndex()) {
            // Normalize packet timestamps to output time base (90000).
            clock.normalizePacket(packet);

            // Write packet to HLS muxer (codec copy to .ts segments).
            if (!muxer.writePacket(packet, videoStream)) {
                LOG_ERROR("Muxer failed to write packet; stopping");
                break;
            }

            health.recordPacketWritten();
            totalBytesWritten += packet->size;
            health.setTotalBytesWritten(totalBytesWritten);
            
            // Update last frame info (PTS in 90kHz units, wall-clock time)
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            health.setLastFrameInfo(packet->pts, now_ms);

            ++videoPacketCount;
            if (videoPacketCount % logInterval == 0) {
                LOG_INFO(
                    "Read %ld video packets so far (last pts=%lld, dts=%lld, size=%d bytes, segments=%d, reconnects=%u)",
                    videoPacketCount,
                    static_cast<long long>(packet->pts),
                    static_cast<long long>(packet->dts),
                    packet->size,
                    muxer.segmentCount(),
                    currentReconnectCount);
            }
        } else {
            ++otherPacketCount;
        }

        av_packet_unref(packet);
    }

    LOG_INFO(
        "Shutting down: %ld video packets, %ld other packets read",
        videoPacketCount,
        otherPacketCount);

    // Request graceful shutdown of source
    source.requestShutdown();

    av_packet_free(&packet);
    muxer.close();
    source.close();

    // Print health report on shutdown
    LOG_INFO("%s", health.getHealthReport().c_str());

    return EXIT_SUCCESS;
}

// Multi-stream mode (Phase 6)
int runMultiStreamMode(const streamer::AppConfig& config) {
    LOG_INFO("Running in multi-stream mode with %zu stream(s)", config.streams.size());

    // Create stream manager
    streamer::StreamManager manager(config);

    // Start all streams
    int started = manager.start();
    if (started == 0) {
        LOG_ERROR("Failed to start any streams");
        return EXIT_FAILURE;
    }

    LOG_INFO("Started %d stream(s); waiting for shutdown signal...", started);

    // Monitor streams until shutdown signal
    const long statusInterval = 500;  // ms
    auto lastStatusTime = std::chrono::system_clock::now();

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

        // Check if all streams have failed
        if (manager.getActiveStreamCount() == 0 && started > 0) {
            LOG_ERROR("All streams have failed or stopped");
            break;
        }
    }

    LOG_INFO("Shutting down all streams...");
    manager.stop();

    // Print final aggregate health
    auto finalHealth = manager.getAggregateHealth();
    LOG_INFO(
        "Final stats: total_packets_written=%llu, total_reconnects=%u, error_streams=%u",
        static_cast<unsigned long long>(finalHealth.total_packets_written),
        finalHealth.total_reconnects,
        finalHealth.error_streams);

    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    const std::string configPath = (argc > 1) ? argv[1] : "config/rtsp-ingest.yaml";

    streamer::AppConfig config;
    try {
        config = streamer::loadConfig(configPath);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load configuration from '%s': %s", configPath.c_str(), e.what());
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, handleShutdownSignal);
    std::signal(SIGTERM, handleShutdownSignal);

    avformat_network_init();

    int result;
    
    // Determine which mode to run based on config
    if (config.isMultiStream()) {
        result = runMultiStreamMode(config);
    } else {
        result = runLegacyMode(config);
    }

    avformat_network_deinit();
    return result;
}
