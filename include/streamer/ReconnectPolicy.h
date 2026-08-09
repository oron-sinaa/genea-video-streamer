#ifndef GENEA_STREAMER_RECONNECT_POLICY_H
#define GENEA_STREAMER_RECONNECT_POLICY_H

#include <chrono>
#include <cstdint>
#include <random>

namespace streamer {

/**
 * Exponential backoff reconnection policy.
 * 
 * Implements exponential backoff with jitter to handle RTSP source disconnections.
 * Formula: delay_ms = min(initial_ms × 2^attempts, max_ms) × (1 ± jitter%)
 * 
 * Example usage:
 *   ReconnectPolicy policy(1000, 30000, 15);  // 1s initial, 30s max, ±15% jitter
 *   while (!connected) {
 *       if (policy.shouldRetry()) {
 *           if (reconnect()) {
 *               policy.reset();
 *               connected = true;
 *           } else {
 *               policy.recordAttempt();
 *           }
 *       }
 *       std::this_thread::sleep_for(10ms);
 *   }
 */
class ReconnectPolicy {
public:
    /**
     * Construct a reconnection policy.
     * 
     * @param initial_delay_ms Initial backoff delay in milliseconds (e.g., 1000)
     * @param max_delay_ms Maximum backoff delay in milliseconds (e.g., 30000)
     * @param jitter_percent Jitter variance as percentage (e.g., 15 for ±15%)
     */
    ReconnectPolicy(uint32_t initial_delay_ms, uint32_t max_delay_ms, uint32_t jitter_percent);

    /**
     * Check if enough time has elapsed to attempt reconnection.
     * 
     * @return true if current time >= next_retry_time, false otherwise
     */
    bool shouldRetry() const;

    /**
     * Get the next retry delay in milliseconds (for logging/debugging).
     * 
     * @return Delay in ms that will be applied after next recordAttempt()
     */
    uint32_t getNextDelayMs() const;

    /**
     * Record a failed reconnection attempt.
     * Increments attempt counter and updates next_retry_time.
     */
    void recordAttempt();

    /**
     * Reset policy after successful reconnection.
     * Sets attempt_count to 0 and next_retry_time to now.
     */
    void reset();

private:
    uint32_t initial_delay_ms_;
    uint32_t max_delay_ms_;
    uint32_t jitter_percent_;
    uint32_t attempt_count_;
    std::chrono::steady_clock::time_point next_retry_time_;
    std::mt19937 rng_;

    /**
     * Calculate the backoff delay for a given attempt number.
     * Applies exponential backoff with max cap.
     */
    uint32_t calculateBackoffMs(uint32_t attempt) const;

    /**
     * Generate jitter factor in range [1.0 - jitter_pct/100, 1.0 + jitter_pct/100].
     */
    double generateJitterFactor();
};

}  // namespace streamer

#endif  // GENEA_STREAMER_RECONNECT_POLICY_H
