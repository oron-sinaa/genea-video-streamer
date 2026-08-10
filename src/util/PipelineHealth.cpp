#include "streamer/PipelineHealth.h"
#include "streamer/Logger.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace streamer {

PipelineHealth::PipelineHealth()
    : start_time_(std::chrono::steady_clock::now()) {
}

void PipelineHealth::recordPacketRead() {
    packets_read_.fetch_add(1, std::memory_order_relaxed);
}

void PipelineHealth::recordPacketWritten() {
    packets_written_.fetch_add(1, std::memory_order_relaxed);
}

void PipelineHealth::recordPacketDropped(const std::string& reason) {
    packets_dropped_.fetch_add(1, std::memory_order_relaxed);
    if (!reason.empty()) {
        LOG_WARN("Packet dropped: %s (total dropped: %lu)", reason.c_str(), packets_dropped_.load());
    }
}

void PipelineHealth::recordReconnect() {
    reconnect_count_.fetch_add(1, std::memory_order_relaxed);
}

void PipelineHealth::setLastFrameInfo(uint64_t pts, int64_t wall_time_ms) {
    last_frame_pts_.store(pts, std::memory_order_release);
    last_packet_timestamp_ms_.store(wall_time_ms, std::memory_order_release);
}

void PipelineHealth::setTotalBytesWritten(uint64_t total_bytes) {
    total_bytes_written_.store(total_bytes, std::memory_order_release);
}

std::string PipelineHealth::formatDuration(uint64_t seconds) {
    uint64_t hours = seconds / 3600;
    uint64_t minutes = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, secs);
    return std::string(buffer);
}

double PipelineHealth::calculateThroughputMbps() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
    
    if (elapsed_ms < 1000) {
        return 0.0;  // Less than 1s elapsed, not enough to calculate meaningful throughput
    }
    
    uint64_t total_bytes = total_bytes_written_.load(std::memory_order_acquire);
    double elapsed_seconds = elapsed_ms / 1000.0;
    double throughput_bps = (total_bytes * 8) / elapsed_seconds;
    return throughput_bps / 1000000.0;  // Convert to Mbps
}

double PipelineHealth::getThroughputMbps() const {
    return calculateThroughputMbps();
}

std::string PipelineHealth::getHealthReport() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
    uint64_t elapsed_seconds = elapsed_ms / 1000;
    
    uint64_t packets_read = packets_read_.load(std::memory_order_acquire);
    uint64_t packets_written = packets_written_.load(std::memory_order_acquire);
    uint64_t packets_dropped = packets_dropped_.load(std::memory_order_acquire);
    uint32_t reconnect_count = reconnect_count_.load(std::memory_order_acquire);
    uint64_t last_frame_pts = last_frame_pts_.load(std::memory_order_acquire);
    int64_t last_packet_time_ms = last_packet_timestamp_ms_.load(std::memory_order_acquire);
    uint64_t total_bytes = total_bytes_written_.load(std::memory_order_acquire);
    
    // Calculate time since last packet
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t time_since_last_packet_s = (now_ms - last_packet_time_ms) / 1000;
    if (time_since_last_packet_s < 0) time_since_last_packet_s = 0;
    
    double throughput_mbps = calculateThroughputMbps();
    
    std::ostringstream oss;
    oss << "\n"
        << "========== Pipeline Health Report ==========\n"
        << "Uptime: " << formatDuration(elapsed_seconds) << "\n"
        << "Packets read: " << packets_read << "\n"
        << "Packets written: " << packets_written << "\n"
        << "Packets dropped: " << packets_dropped << "\n"
        << "Reconnects: " << reconnect_count << "\n"
        << "Last frame PTS: " << last_frame_pts << " (90kHz units)\n"
        << "Last packet received: " << time_since_last_packet_s << " seconds ago\n"
        << "Total bytes written: " << total_bytes << " bytes\n"
        << "Average throughput: " << std::fixed << std::setprecision(2) << throughput_mbps << " Mbps\n"
        << "============================================\n";
    
    return oss.str();
}

}  // namespace streamer
