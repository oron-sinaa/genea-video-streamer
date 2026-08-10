#include "streamer/ReconnectPolicy.h"
#include "streamer/Logger.h"

#include <algorithm>
#include <cmath>

namespace streamer {

ReconnectPolicy::ReconnectPolicy(uint32_t initial_delay_ms, uint32_t max_delay_ms, uint32_t jitter_percent)
    : initial_delay_ms_(initial_delay_ms),
      max_delay_ms_(max_delay_ms),
      jitter_percent_(jitter_percent),
      attempt_count_(0),
      next_retry_time_(std::chrono::steady_clock::now()),
      rng_(std::random_device{}()) {
}

bool ReconnectPolicy::shouldRetry() const {
    return std::chrono::steady_clock::now() >= next_retry_time_;
}

uint32_t ReconnectPolicy::getNextDelayMs() const {
    return calculateBackoffMs(attempt_count_);
}

void ReconnectPolicy::recordAttempt() {
    uint32_t backoff_ms = calculateBackoffMs(attempt_count_);
    double jitter_factor = generateJitterFactor();
    uint32_t final_delay_ms = static_cast<uint32_t>(backoff_ms * jitter_factor);
    
    LOG_INFO("Reconnection attempt %u: waiting %u ms before retry (backoff: %u ms, jitter: %.2f)",
             attempt_count_, final_delay_ms, backoff_ms, jitter_factor);
    
    attempt_count_++;
    next_retry_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(final_delay_ms);
}

void ReconnectPolicy::reset() {
    LOG_INFO("Reconnection successful, resetting backoff policy");
    attempt_count_ = 0;
    next_retry_time_ = std::chrono::steady_clock::now();
}

uint32_t ReconnectPolicy::calculateBackoffMs(uint32_t attempt) const {
    // Exponential: initial × 2^attempt, capped at max
    if (attempt >= 32) {
        // Avoid overflow: if attempt >= 32, 2^32 is huge, so just return max
        return max_delay_ms_;
    }
    
    uint32_t exponential = initial_delay_ms_ * (1U << attempt);  // 1U << attempt == 2^attempt
    return std::min(exponential, max_delay_ms_);
}

double ReconnectPolicy::generateJitterFactor() {
    // Generate random value in [-jitter_percent, +jitter_percent]
    std::uniform_int_distribution<int> dist(-static_cast<int>(jitter_percent_), static_cast<int>(jitter_percent_));
    int jitter_offset = dist(rng_);
    return 1.0 + (jitter_offset / 100.0);
}

}  // namespace streamer
