#include "streamer/PipelineHealth.h"

#include <cassert>
#include <iostream>
#include <thread>

using namespace streamer;

void test_packet_counters() {
    PipelineHealth health;
    
    // Initial state
    assert(health.getPacketsRead() == 0);
    assert(health.getPacketsWritten() == 0);
    assert(health.getPacketsDropped() == 0);
    assert(health.getReconnectCount() == 0);
    
    // Record some packets
    health.recordPacketRead();
    assert(health.getPacketsRead() == 1);
    
    health.recordPacketRead();
    assert(health.getPacketsRead() == 2);
    
    health.recordPacketWritten();
    assert(health.getPacketsWritten() == 1);
    
    health.recordPacketDropped("test reason");
    assert(health.getPacketsDropped() == 1);
    
    std::cout << "✓ test_packet_counters PASSED\n";
}

void test_reconnect_count() {
    PipelineHealth health;
    
    assert(health.getReconnectCount() == 0);
    
    health.recordReconnect();
    assert(health.getReconnectCount() == 1);
    
    health.recordReconnect();
    assert(health.getReconnectCount() == 2);
    
    std::cout << "✓ test_reconnect_count PASSED\n";
}

void test_health_report_format() {
    PipelineHealth health;
    
    // Generate some activity
    health.recordPacketRead();
    health.recordPacketRead();
    health.recordPacketWritten();
    health.recordReconnect();
    
    std::string report = health.getHealthReport();
    
    // Verify report contains expected keywords
    assert(report.find("Pipeline Health Report") != std::string::npos);
    assert(report.find("Uptime") != std::string::npos);
    assert(report.find("Packets read: 2") != std::string::npos);
    assert(report.find("Packets written: 1") != std::string::npos);
    assert(report.find("Reconnects: 1") != std::string::npos);
    assert(report.find("Mbps") != std::string::npos);
    
    std::cout << "✓ test_health_report_format PASSED\n";
}

void test_frame_info_tracking() {
    PipelineHealth health;
    
    int64_t test_pts = 123456;
    int64_t test_time_ms = 1000000000;
    
    health.setLastFrameInfo(test_pts, test_time_ms);
    assert(health.getLastFramePts() == test_pts);
    
    // Update again
    health.setLastFrameInfo(test_pts + 90000, test_time_ms + 1000);  // Next 1-second frame
    assert(health.getLastFramePts() == test_pts + 90000);
    
    std::cout << "✓ test_frame_info_tracking PASSED\n";
}

void test_throughput_calculation() {
    PipelineHealth health;
    
    // Write some bytes over a known time
    uint64_t total_bytes = 1000000;  // 1 MB
    health.setTotalBytesWritten(total_bytes);
    
    // Sleep a bit to allow some elapsed time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get health report which includes throughput
    std::string report = health.getHealthReport();
    
    // Verify throughput is in the report (won't test exact value due to timing variance)
    assert(report.find("Mbps") != std::string::npos);
    
    std::cout << "✓ test_throughput_calculation PASSED\n";
}

void test_monotonic_counters() {
    // Verify counters never decrease (monotonic)
    PipelineHealth health;
    
    health.recordPacketRead();
    auto first_read = health.getPacketsRead();
    
    health.recordPacketRead();
    auto second_read = health.getPacketsRead();
    
    assert(second_read >= first_read);
    assert(second_read == first_read + 1);
    
    std::cout << "✓ test_monotonic_counters PASSED\n";
}

int main() {
    std::cout << "\n=== PipelineHealth Unit Tests ===\n\n";
    
    try {
        test_packet_counters();
        test_reconnect_count();
        test_health_report_format();
        test_frame_info_tracking();
        test_throughput_calculation();
        test_monotonic_counters();
        
        std::cout << "\n=== All tests PASSED ===\n\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
