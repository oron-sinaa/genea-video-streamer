#ifndef GENEA_STREAMER_PIPELINE_HEALTH_H
#define GENEA_STREAMER_PIPELINE_HEALTH_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace streamer {

/**
 * Tracks pipeline health metrics: packets, reconnects, timing, throughput.
 * 
 * All counters are atomic to support thread-safe access from multiple components.
 * Metrics are never reset during runtime (monotonically increasing).
 * 
 * Example usage:
 *   PipelineHealth health;
 *   // During streaming:
 *   health.recordPacketRead();
 *   health.recordPacketWritten();
 *   health.recordReconnect();
 *   // On shutdown:
 *   std::cout << health.getHealthReport() << std::endl;
 */
class PipelineHealth {
public:
    PipelineHealth();

    /**
     * Record a packet read from RTSP source.
     */
    void recordPacketRead();

    /**
     * Record a packet written to HLS segments.
     */
    void recordPacketWritten();

    /**
     * Record a dropped packet (e.g., codec incompatible, malformed).
     * 
     * @param reason Human-readable reason for drop (e.g., "codec incompatible", "AV_NOPTS_VALUE")
     */
    void recordPacketDropped(const std::string& reason = "");

    /**
     * Record a reconnection event.
     */
    void recordReconnect();

    /**
     * Update last frame information (PTS and wall-clock time).
     * 
     * @param pts Presentation timestamp in 90kHz units
     * @param wall_time_ms Wall-clock time in milliseconds since epoch
     */
    void setLastFrameInfo(uint64_t pts, int64_t wall_time_ms);

    /**
     * Update total bytes written to disk.
     * 
     * @param total_bytes Total cumulative bytes written
     */
    void setTotalBytesWritten(uint64_t total_bytes);

    /**
     * Generate a human-readable health report.
     * Includes uptime, packet counts, reconnects, throughput, last frame info.
     * 
     * @return Multi-line formatted health report
     */
    std::string getHealthReport() const;

    // Accessors (for testing/debugging)
    uint64_t getPacketsRead() const { return packets_read_.load(); }
    uint64_t getPacketsWritten() const { return packets_written_.load(); }
    uint64_t getPacketsDropped() const { return packets_dropped_.load(); }
    uint32_t getReconnectCount() const { return reconnect_count_.load(); }
    uint64_t getLastFramePts() const { return last_frame_pts_.load(); }

private:
    std::atomic<uint64_t> packets_read_{0};
    std::atomic<uint64_t> packets_written_{0};
    std::atomic<uint64_t> packets_dropped_{0};
    std::atomic<uint32_t> reconnect_count_{0};
    std::atomic<uint64_t> last_frame_pts_{0};
    std::atomic<int64_t> last_packet_timestamp_ms_{0};
    std::atomic<uint64_t> total_bytes_written_{0};
    std::chrono::steady_clock::time_point start_time_;

    /**
     * Format a duration in seconds to HH:MM:SS format.
     */
    static std::string formatDuration(uint64_t seconds);

    /**
     * Calculate average throughput in Mbps.
     */
    double calculateThroughputMbps() const;
};

}  // namespace streamer

#endif  // GENEA_STREAMER_PIPELINE_HEALTH_H
