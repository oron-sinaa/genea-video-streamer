// Quick test to verify HLS config parsing.
#include "streamer/Config.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

int main() {
    std::printf("Testing HLS config parsing...\n");

    try {
        const auto config = streamer::loadConfig("/home/ubuntu/aanis/genea-video-streamer/config/rtsp-ingest.yaml");

        std::printf("RTSP config:\n");
        std::printf("  url: %s\n", config.rtsp.url.c_str());
        std::printf("  transport: %s\n", config.rtsp.transport.c_str());
        std::printf("  timeout_us: %d\n", config.rtsp.timeout_us);

        std::printf("\nHLS config:\n");
        std::printf("  output_dir: %s\n", config.hls.output_dir.c_str());
        std::printf("  segment_duration_s: %d\n", config.hls.segment_duration_s);
        std::printf("  archive_retention_hours: %d\n", config.hls.archive_retention_hours);
        std::printf("  cleanup_interval_s: %d\n", config.hls.cleanup_interval_s);

        // Verify parsed values
        assert(config.hls.output_dir == "segments");
        assert(config.hls.segment_duration_s == 3);
        assert(config.hls.archive_retention_hours == 2);
        assert(config.hls.cleanup_interval_s == 300);

        std::printf("\n✓ All HLS config tests passed!\n");
        return 0;
    } catch (const std::exception& e) {
        std::printf("✗ Config test failed: %s\n", e.what());
        return 1;
    }
}
