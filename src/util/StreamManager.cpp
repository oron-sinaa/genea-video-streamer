#include "streamer/StreamManager.h"

#include "streamer/Logger.h"

namespace streamer {

StreamManager::StreamManager(const AppConfig& config) : config_(config), lastError_("") {
    LOG_INFO("StreamManager initialized for %zu stream(s)", config.streams.size());
    createWorkers();
}

StreamManager::~StreamManager() {
    try {
        // Only stop if not already stopped
        // Note: stop() is idempotent and checks if workers are still active
        if (!workers_.empty()) {
            // Check if any worker is still in a running state
            bool anyRunning = false;
            for (const auto& worker : workers_) {
                if (worker && worker->isActive()) {
                    anyRunning = true;
                    break;
                }
            }
            if (anyRunning) {
                stop();
            }
        }
        workers_.clear();
    } catch (const std::exception& e) {
        LOG_ERROR("StreamManager destructor: Exception during cleanup: %s", e.what());
    } catch (...) {
        LOG_ERROR("StreamManager destructor: Unknown exception during cleanup");
    }
}

void StreamManager::createWorkers() {
    workers_.clear();

    for (const auto& streamConfig : config_.streams) {
        try {
            StreamWorker::WorkerConfig workerConfig;
            workerConfig.stream_name = streamConfig.name;
            workerConfig.rtsp_url = streamConfig.rtsp_url;
            workerConfig.hls_output_dir = streamConfig.hls_output;
            workerConfig.reconnect = {streamConfig.reconnect.enabled,
                                      streamConfig.reconnect.initial_delay_ms,
                                      streamConfig.reconnect.max_delay_ms,
                                      streamConfig.reconnect.jitter_percent,
                                      streamConfig.reconnect.stale_timeout_s};
            workerConfig.hls_config = config_.hls;
            workerConfig.hls_config.output_dir = streamConfig.hls_output;

            workers_.push_back(std::make_unique<StreamWorker>(workerConfig));
            LOG_INFO("StreamManager created worker for stream '%s'", streamConfig.name.c_str());
        } catch (const std::exception& e) {
            lastError_ = std::string("Failed to create worker for stream '") + streamConfig.name +
                        "': " + e.what();
            LOG_ERROR("StreamManager %s", lastError_.c_str());
        }
    }

    LOG_INFO("StreamManager created %zu worker(s)", workers_.size());
}

int StreamManager::start() {
    int successCount = 0;
    int failCount = 0;

    LOG_INFO("StreamManager starting %zu stream(s)...", workers_.size());

    for (auto& worker : workers_) {
        if (worker && worker->start()) {
            ++successCount;
            LOG_INFO("StreamManager started stream '%s'", worker->getName().c_str());
        } else {
            ++failCount;
            lastError_ = worker ? worker->getLastError() : "Unknown error";
            LOG_ERROR("StreamManager failed to start stream '%s': %s",
                     worker ? worker->getName().c_str() : "?", lastError_.c_str());
        }
    }

    LOG_INFO("StreamManager started %d stream(s), %d failed", successCount, failCount);
    return successCount;
}

void StreamManager::stop() {
    // Count active streams before stopping
    int activeCount = 0;
    for (const auto& worker : workers_) {
        if (worker && worker->isActive()) {
            ++activeCount;
        }
    }

    // Only log and stop if there are active streams
    if (activeCount > 0) {
        LOG_INFO("StreamManager stopping all %d active stream(s) (out of %zu total)...", activeCount, workers_.size());

        for (auto& worker : workers_) {
            if (worker && worker->isActive()) {
                LOG_INFO("StreamManager stopping stream '%s'", worker->getName().c_str());
                worker->stop();
            }
        }

        LOG_INFO("StreamManager all streams stopped");
    }
}

StreamWorker* StreamManager::getStream(const std::string& name) {
    for (auto& worker : workers_) {
        if (worker && worker->getName() == name) {
            return worker.get();
        }
    }
    return nullptr;
}

StreamManager::AggregateHealth StreamManager::getAggregateHealth() const {
    AggregateHealth aggregate;

    for (const auto& worker : workers_) {
        if (!worker) continue;

        const auto& health = worker->getHealth();

        aggregate.total_packets_read += health.getPacketsRead();
        aggregate.total_packets_written += health.getPacketsWritten();
        aggregate.total_packets_dropped += health.getPacketsDropped();
        aggregate.total_reconnects += health.getReconnectCount();

        if (worker->isActive()) {
            ++aggregate.active_streams;
        }

        if (worker->getStatus() == StreamWorker::Status::ERROR) {
            ++aggregate.error_streams;
        }

        // Calculate per-stream throughput
        std::string report = health.getHealthReport();
        double throughput = 0.0;  // TODO: Parse from report or add getter method
        aggregate.total_throughput_mbps += throughput;

        // Add per-stream stats
        StreamStats stat;
        stat.name = worker->getName();
        stat.status = worker->getStatus();
        stat.packets_written = health.getPacketsWritten();
        stat.throughput_mbps = throughput;
        stat.reconnects = health.getReconnectCount();
        aggregate.stream_stats.push_back(stat);
    }

    return aggregate;
}

bool StreamManager::allStreamsActive() const {
    if (workers_.empty()) return false;
    
    for (const auto& worker : workers_) {
        if (!worker || !worker->isActive()) {
            return false;
        }
    }
    return true;
}

int StreamManager::getActiveStreamCount() const {
    int count = 0;
    for (const auto& worker : workers_) {
        if (worker && worker->isActive()) {
            ++count;
        }
    }
    return count;
}

}  // namespace streamer
