#pragma once

#include "streamer/Config.h"
#include "streamer/HlsMuxer.h"
#include "streamer/PacketClock.h"
#include "streamer/PipelineHealth.h"
#include "streamer/RtspSource.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace streamer {

// StreamWorker encapsulates a single-stream pipeline (RTSP source → HLS output)
// with independent health tracking, reconnect policy, and thread management.
//
// Each StreamWorker operates independently:
// - Separate RTSP connection
// - Separate HLS segment directory
// - Independent reconnect policy
// - Per-stream health metrics
//
// Designed for concurrent operation within StreamManager.
class StreamWorker {
   public:
    // Configuration for a single stream
    struct WorkerConfig {
        std::string stream_name;           // Unique identifier (e.g., "camera-1")
        std::string rtsp_url;              // RTSP source URL
        std::string hls_output_dir;        // Per-stream HLS directory
        struct {
            bool enabled = true;
            uint32_t initial_delay_ms = 1000;
            uint32_t max_delay_ms = 30000;
            uint32_t jitter_percent = 15;
            uint32_t stale_timeout_s = 10;
        } reconnect;
        HlsConfig hls_config;  // HLS muxer configuration
    };

    // Status of the worker
    enum class Status {
        IDLE,          // Not started or stopped
        RUNNING,       // Active and processing packets
        RECONNECTING,  // Attempting to reconnect to RTSP source
        ERROR,         // Encountered unrecoverable error
        SHUTTING_DOWN  // Graceful shutdown in progress
    };

    // Constructor: Initialize worker with configuration
    StreamWorker(const WorkerConfig& config);

    // Destructor: Cleanup resources
    ~StreamWorker();

    // Start the worker (begins packet processing thread)
    // Returns true on success, false if already running or initialization fails
    bool start();

    // Stop the worker (gracefully shuts down packet processing)
    // Blocks until thread terminates
    void stop();

    // Get current worker status
    Status getStatus() const;

    // Get stream name
    std::string getName() const { return config_.stream_name; }

    // Get HLS output directory for this stream
    std::string getHlsOutputDir() const { return config_.hls_output_dir; }

    // Get per-stream health metrics
    const PipelineHealth& getHealth() const { return health_; }

    // Get mutable reference to health metrics (for metrics aggregation)
    PipelineHealth& getHealth() { return health_; }

    // Check if worker is active and processing
    bool isActive() const;

    // Get last error message (if status is ERROR)
    std::string getLastError() const;

   private:
    // Main processing loop (runs in separate thread)
    void processingLoop();

    // Initialize RTSP source
    bool initializeSource();

    // Initialize HLS muxer
    bool initializeMuxer();

    // Clean up resources
    void cleanup();

    WorkerConfig config_;
    std::string lastError_;

    // Thread management
    std::unique_ptr<std::thread> workerThread_;
    std::atomic<bool> shutdownRequested_{false};
    bool stopped_ = false;  // Prevents double-stops

    // Pipeline components
    std::unique_ptr<RtspSource> rtspSource_;
    std::unique_ptr<HlsMuxer> hlsMuxer_;
    std::unique_ptr<PacketClock> packetClock_;

    // Per-stream health tracking
    PipelineHealth health_;

    // Synchronization - Use only atomicStatus_ as the source of truth
    mutable std::atomic<Status> atomicStatus_{Status::IDLE};
};

}  // namespace streamer
