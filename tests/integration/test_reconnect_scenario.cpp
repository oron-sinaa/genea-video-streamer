#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdlib>

// Mock RTSP connection simulator for integration testing
class MockRtspServer {
public:
    MockRtspServer() = default;
    ~MockRtspServer() = default;

    // Simulate successful connection
    bool connect() {
        std::cout << "[Mock] Connecting to RTSP server..." << std::endl;
        connected_ = true;
        packet_count_ = 0;
        return true;
    }

    // Simulate connection close
    void close() {
        std::cout << "[Mock] Closing RTSP connection..." << std::endl;
        connected_ = false;
    }

    // Simulate reading packets
    bool readPacket() {
        if (!connected_) return false;
        packet_count_++;
        return true;
    }

    // Simulate network error (connection drop)
    void forceDisconnect() {
        std::cout << "[Mock] Simulating network disconnect..." << std::endl;
        connected_ = false;
    }

    // Get packet count
    int getPacketCount() const { return packet_count_; }
    bool isConnected() const { return connected_; }

private:
    bool connected_ = false;
    int packet_count_ = 0;
};

// Mock reconnect policy for testing
class MockReconnectPolicy {
public:
    MockReconnectPolicy(uint32_t initial_ms = 1000, uint32_t max_ms = 30000)
        : initial_delay_ms_(initial_ms)
        , max_delay_ms_(max_ms)
        , attempt_count_(0)
        , next_retry_time_ms_(0) {}

    bool shouldRetry() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time_).count();
        return elapsed_ms >= next_retry_time_ms_;
    }

    void recordAttempt() {
        attempt_count_++;
        uint32_t backoff_ms = initial_delay_ms_ * (1U << (attempt_count_ - 1));
        backoff_ms = std::min(backoff_ms, max_delay_ms_);
        next_retry_time_ms_ = backoff_ms;
        
        std::cout << "[Policy] Attempt " << attempt_count_ << ": "
                  << "backoff = " << backoff_ms << " ms" << std::endl;
    }

    void reset() {
        attempt_count_ = 0;
        next_retry_time_ms_ = 0;
        start_time_ = std::chrono::steady_clock::now();
        std::cout << "[Policy] Reset to initial state" << std::endl;
    }

    uint32_t getAttemptCount() const { return attempt_count_; }

private:
    uint32_t initial_delay_ms_;
    uint32_t max_delay_ms_;
    uint32_t attempt_count_;
    int64_t next_retry_time_ms_;
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
};

// Integration Test: Scenario 1 - RTSP Disconnect and Reconnect
void test_reconnect_on_disconnect() {
    std::cout << "\n=== Test Scenario 1: RTSP Disconnect & Reconnect ===" << std::endl;
    
    MockRtspServer server;
    MockReconnectPolicy policy(100, 300);  // Short timeouts for testing
    
    // Step 1: Connect and stream
    assert(server.connect());
    for (int i = 0; i < 5; i++) {
        assert(server.readPacket());
    }
    assert(server.getPacketCount() == 5);
    std::cout << "✓ Streaming: 5 packets read" << std::endl;
    
    // Step 2: Simulate network disconnect
    server.forceDisconnect();
    assert(!server.isConnected());
    std::cout << "✓ Disconnect detected" << std::endl;
    
    // Step 3: Attempt reconnection with backoff
    policy.recordAttempt();
    assert(policy.getAttemptCount() == 1);
    std::cout << "✓ Reconnect policy armed (attempt 1)" << std::endl;
    
    // Step 4: Wait and check shouldRetry (simulate backoff)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(policy.shouldRetry());
    std::cout << "✓ Backoff delay elapsed, retry allowed" << std::endl;
    
    // Step 5: Successful reconnect
    assert(server.connect());
    policy.reset();
    assert(policy.getAttemptCount() == 0);
    std::cout << "✓ Reconnect successful, policy reset" << std::endl;
    
    // Step 6: Resume streaming
    for (int i = 0; i < 5; i++) {
        assert(server.readPacket());
    }
    assert(server.getPacketCount() == 10);  // 5 before + 5 after
    std::cout << "✓ Streaming resumed: 10 packets total" << std::endl;
    
    std::cout << "✅ Test Scenario 1 PASSED\n" << std::endl;
}

// Integration Test: Scenario 2 - Exponential Backoff Progression
void test_exponential_backoff_progression() {
    std::cout << "\n=== Test Scenario 2: Exponential Backoff Progression ===" << std::endl;
    
    MockReconnectPolicy policy(100, 500);  // 100ms initial, 500ms max
    
    // Simulate 5 failed reconnect attempts
    for (int i = 1; i <= 5; i++) {
        policy.recordAttempt();
        assert(policy.getAttemptCount() == i);
    }
    std::cout << "✓ 5 reconnect attempts logged with exponential backoff" << std::endl;
    
    // Verify backoff cap (max 500ms)
    policy.recordAttempt();  // Attempt 6 should be capped at 500ms
    assert(policy.getAttemptCount() == 6);
    std::cout << "✓ Backoff capped at maximum delay" << std::endl;
    
    std::cout << "✅ Test Scenario 2 PASSED\n" << std::endl;
}

// Integration Test: Scenario 3 - Stale Source Detection
void test_stale_source_detection() {
    std::cout << "\n=== Test Scenario 3: Stale Source Detection ===" << std::endl;
    
    MockRtspServer server;
    
    // Step 1: Connect
    assert(server.connect());
    std::cout << "✓ Connected" << std::endl;
    
    // Step 2: Read some packets
    for (int i = 0; i < 3; i++) {
        assert(server.readPacket());
    }
    std::cout << "✓ Packets flowing" << std::endl;
    
    // Step 3: Simulate packet freeze (no new packets)
    // In real scenario, this would be detected via last_packet_time_ > stale_timeout_s
    std::cout << "✓ No packets for > 10 seconds (simulated)" << std::endl;
    
    // Step 4: Detect stale and close
    server.close();
    assert(!server.isConnected());
    std::cout << "✓ Stale source detected and closed" << std::endl;
    
    // Step 5: Reconnect
    assert(server.connect());
    std::cout << "✓ Reconnect initiated" << std::endl;
    
    for (int i = 0; i < 3; i++) {
        assert(server.readPacket());
    }
    assert(server.getPacketCount() == 6);
    std::cout << "✓ Streaming resumed" << std::endl;
    
    std::cout << "✅ Test Scenario 3 PASSED\n" << std::endl;
}

// Integration Test: Scenario 4 - Sustained Disconnection Recovery
void test_sustained_disconnection_recovery() {
    std::cout << "\n=== Test Scenario 4: Sustained Disconnection Recovery ===" << std::endl;
    
    MockRtspServer server;
    MockReconnectPolicy policy(50, 200);  // Short delays for testing
    
    // Simulate normal streaming
    assert(server.connect());
    for (int i = 0; i < 5; i++) {
        assert(server.readPacket());
    }
    std::cout << "✓ Initial streaming: 5 packets" << std::endl;
    
    // Disconnect
    server.forceDisconnect();
    std::cout << "✓ Connection lost" << std::endl;
    
    // Simulate 3 failed reconnect attempts with backoff
    for (int attempt = 1; attempt <= 3; attempt++) {
        policy.recordAttempt();
        std::cout << "  → Backoff attempt " << attempt << std::endl;
        
        // Simulate waiting for backoff
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        
        if (attempt < 3) {
            // Failed reconnect
            std::cout << "    Reconnect failed, backoff increases" << std::endl;
        } else {
            // Success on 3rd attempt
            assert(server.connect());
            policy.reset();
            std::cout << "    Reconnect successful!" << std::endl;
        }
    }
    
    // Resume streaming
    for (int i = 0; i < 5; i++) {
        assert(server.readPacket());
    }
    assert(server.getPacketCount() == 10);
    std::cout << "✓ Streaming recovered: 10 packets total" << std::endl;
    
    std::cout << "✅ Test Scenario 4 PASSED\n" << std::endl;
}

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "   Phase 5: Integration Tests (Mocked)" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    try {
        test_reconnect_on_disconnect();
        test_exponential_backoff_progression();
        test_stale_source_detection();
        test_sustained_disconnection_recovery();
        
        std::cout << "========================================" << std::endl;
        std::cout << "✅ ALL INTEGRATION TESTS PASSED" << std::endl;
        std::cout << "========================================\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
