#pragma once

#include "streamer/Config.h"
#include "streamer/PipelineHealth.h"
#include "streamer/StreamWorker.h"

#include <memory>
#include <string>
#include <vector>

namespace streamer {

// Per-stream statistics for aggregate health reporting
struct StreamStats {
    std::string name;
    StreamWorker::Status status;
    uint64_t packets_written;
    double throughput_mbps;
    uint32_t reconnects;
};

// StreamManager orchestrates multiple concurrent StreamWorker instances
// and provides aggregate health metrics and lifecycle management.
//
// Responsibilities:
// - Create and manage StreamWorker instances
// - Monitor all streams concurrently
// - Provide aggregate health metrics (total packets, reconnects, etc.)
// - Handle graceful shutdown across all streams
// - Report per-stream status via REST API
class StreamManager {
   public:
    // Constructor: Initialize manager with multi-stream config
    explicit StreamManager(const AppConfig& config);

    // Destructor: Clean up all workers
    ~StreamManager();

    // Start all configured streams
    // Returns number of successfully started streams
    int start();

    // Stop all streams gracefully
    void stop();

    // Get total number of managed streams
    size_t getStreamCount() const { return workers_.size(); }

    // Get status of a specific stream by name
    // Returns nullptr if stream not found
    StreamWorker* getStream(const std::string& name);

    // Get aggregate health metrics across all streams
    // Returns struct containing totals and per-stream info
    struct AggregateHealth {
        uint64_t total_packets_read = 0;
        uint64_t total_packets_written = 0;
        uint64_t total_packets_dropped = 0;
        uint32_t total_reconnects = 0;
        double total_throughput_mbps = 0.0;
        uint32_t active_streams = 0;
        uint32_t error_streams = 0;
        std::vector<StreamStats> stream_stats;
    };

    AggregateHealth getAggregateHealth() const;

    // Get last error message (if any stream failed)
    std::string getLastError() const { return lastError_; }

    // Check if all streams are active
    bool allStreamsActive() const;

    // Get number of streams that are currently active
    int getActiveStreamCount() const;

   private:
    AppConfig config_;
    std::vector<std::unique_ptr<StreamWorker>> workers_;
    mutable std::string lastError_;
    bool stopped_ = false;  // Track if stop() has been called to prevent double-stops

    // Helper to create workers from config
    void createWorkers();
};

}  // namespace streamer
