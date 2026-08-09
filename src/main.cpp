#include "streamer/Config.h"
#include "streamer/Logger.h"
#include "streamer/RtspSource.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

// Set from a signal handler; only safe operation is a lock-free store/load.
std::atomic<bool> g_shutdownRequested{false};

void handleShutdownSignal(int /*signal*/) {
    g_shutdownRequested.store(true);
}

}  // namespace

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

    streamer::RtspSource source(config.rtsp);
    if (!source.open()) {
        LOG_ERROR("Failed to open RTSP source; exiting");
        avformat_network_deinit();
        return EXIT_FAILURE;
    }

    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        LOG_ERROR("Failed to allocate AVPacket");
        source.close();
        avformat_network_deinit();
        return EXIT_FAILURE;
    }

    LOG_INFO("Starting packet read loop (Ctrl+C to stop)...");

    long videoPacketCount = 0;
    long otherPacketCount = 0;
    const long logInterval = 100;

    while (!g_shutdownRequested.load()) {
        if (!source.readPacket(packet)) {
            LOG_ERROR("Packet read failed or stream ended; stopping");
            break;
        }

        if (packet->stream_index == source.videoStreamIndex()) {
            ++videoPacketCount;
            if (videoPacketCount % logInterval == 0) {
                LOG_INFO(
                    "Read %ld video packets so far (last pts=%lld, dts=%lld, size=%d bytes)",
                    videoPacketCount,
                    static_cast<long long>(packet->pts),
                    static_cast<long long>(packet->dts),
                    packet->size);
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

    av_packet_free(&packet);
    source.close();
    avformat_network_deinit();

    return EXIT_SUCCESS;
}
