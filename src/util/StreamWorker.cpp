#include "streamer/StreamWorker.h"

#include "streamer/Logger.h"

#include <chrono>
#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace streamer {

StreamWorker::StreamWorker(const WorkerConfig& config)
    : config_(config), status_(Status::IDLE), lastError_("") {
    LOG_INFO("StreamWorker[%s] initialized with RTSP URL: %s", config_.stream_name.c_str(),
             config_.rtsp_url.c_str());
}

StreamWorker::~StreamWorker() {
    try {
        stop();
        cleanup();
    } catch (const std::exception& e) {
        // Log but don't throw from destructor
        LOG_ERROR("StreamWorker destructor: Exception during cleanup: %s", e.what());
    } catch (...) {
        LOG_ERROR("StreamWorker destructor: Unknown exception during cleanup");
    }
}

bool StreamWorker::start() {
    if (status_.load() != Status::IDLE) {
        LOG_WARN("StreamWorker[%s] already running or in error state", config_.stream_name.c_str());
        return false;
    }

    try {
        LOG_INFO("StreamWorker[%s] starting...", config_.stream_name.c_str());
        shutdownRequested_.store(false);
        atomicStatus_.store(Status::RUNNING);
        
        workerThread_ = std::make_unique<std::thread>([this]() { processingLoop(); });
        
        // Give thread a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (atomicStatus_.load() == Status::ERROR) {
            LOG_ERROR("StreamWorker[%s] failed to start: %s", config_.stream_name.c_str(),
                     lastError_.c_str());
            return false;
        }
        
        LOG_INFO("StreamWorker[%s] started successfully", config_.stream_name.c_str());
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Start failed: ") + e.what();
        atomicStatus_.store(Status::ERROR);
        LOG_ERROR("StreamWorker[%s] exception during start: %s", config_.stream_name.c_str(),
                 e.what());
        return false;
    }
}

void StreamWorker::stop() {
    if (status_.load() == Status::IDLE) {
        return;  // Already stopped
    }

    LOG_INFO("StreamWorker[%s] stopping...", config_.stream_name.c_str());
    atomicStatus_.store(Status::SHUTTING_DOWN);
    shutdownRequested_.store(true);

    if (rtspSource_) {
        rtspSource_->requestShutdown();
    }

    // Wait for thread to complete
    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }

    cleanup();
    atomicStatus_.store(Status::IDLE);
    LOG_INFO("StreamWorker[%s] stopped", config_.stream_name.c_str());
}

StreamWorker::Status StreamWorker::getStatus() const {
    return atomicStatus_.load();
}

bool StreamWorker::isActive() const {
    Status s = atomicStatus_.load();
    return s == Status::RUNNING || s == Status::RECONNECTING;
}

std::string StreamWorker::getLastError() const {
    return lastError_;
}

bool StreamWorker::initializeSource() {
    try {
        // Create RtspConfig from stream config
        RtspConfig rtspConfig;
        rtspConfig.url = config_.rtsp_url;
        rtspConfig.reconnect_enabled = config_.reconnect.enabled;
        rtspConfig.reconnect_initial_delay_ms = config_.reconnect.initial_delay_ms;
        rtspConfig.reconnect_max_delay_ms = config_.reconnect.max_delay_ms;
        rtspConfig.reconnect_jitter_percent = config_.reconnect.jitter_percent;
        rtspConfig.stale_timeout_s = config_.reconnect.stale_timeout_s;

        rtspSource_ = std::make_unique<RtspSource>(rtspConfig, &health_);

        if (!rtspSource_->open()) {
            lastError_ = "Failed to open RTSP source";
            LOG_ERROR("StreamWorker[%s] %s", config_.stream_name.c_str(), lastError_.c_str());
            return false;
        }

        LOG_INFO("StreamWorker[%s] RTSP source opened successfully", config_.stream_name.c_str());
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("RTSP initialization failed: ") + e.what();
        LOG_ERROR("StreamWorker[%s] %s", config_.stream_name.c_str(), lastError_.c_str());
        return false;
    }
}

bool StreamWorker::initializeMuxer() {
    try {
        hlsMuxer_ = std::make_unique<HlsMuxer>(config_.hls_config);

        if (!hlsMuxer_->open()) {
            lastError_ = "Failed to initialize HLS muxer";
            LOG_ERROR("StreamWorker[%s] %s", config_.stream_name.c_str(), lastError_.c_str());
            return false;
        }

        // Initialize packet clock for timestamp normalization
        AVStream* videoStream = rtspSource_->videoStream();
        if (videoStream == nullptr) {
            lastError_ = "Failed to get video stream";
            LOG_ERROR("StreamWorker[%s] %s", config_.stream_name.c_str(), lastError_.c_str());
            return false;
        }

        packetClock_ = std::make_unique<PacketClock>(videoStream, 90000);
        LOG_INFO("StreamWorker[%s] HLS muxer initialized", config_.stream_name.c_str());
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("HLS initialization failed: ") + e.what();
        LOG_ERROR("StreamWorker[%s] %s", config_.stream_name.c_str(), lastError_.c_str());
        return false;
    }
}

void StreamWorker::cleanup() {
    if (hlsMuxer_) {
        hlsMuxer_->close();
        hlsMuxer_.reset();
    }
    if (rtspSource_) {
        rtspSource_->close();
        rtspSource_.reset();
    }
    packetClock_.reset();
}

void StreamWorker::processingLoop() {
    LOG_INFO("StreamWorker[%s] processing thread started", config_.stream_name.c_str());

    // Initialize pipeline components
    if (!initializeSource()) {
        atomicStatus_.store(Status::ERROR);
        return;
    }

    if (!initializeMuxer()) {
        atomicStatus_.store(Status::ERROR);
        return;
    }

    atomicStatus_.store(Status::RUNNING);

    // Packet reading loop
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        lastError_ = "Failed to allocate AVPacket";
        atomicStatus_.store(Status::ERROR);
        return;
    }

    uint32_t lastReconnectCount = health_.getReconnectCount();
    long videoPacketCount = 0;
    uint64_t totalBytesWritten = 0;
    const long logInterval = 100;

    while (!shutdownRequested_.load()) {
        try {
            // Read packet from RTSP source (with reconnect logic built-in)
            if (!rtspSource_->readPacket(packet)) {
                LOG_WARN("StreamWorker[%s] packet read failed or stream ended", 
                        config_.stream_name.c_str());
                break;
            }

            // Check for reconnect and write discontinuity marker
            uint32_t currentReconnectCount = health_.getReconnectCount();
            if (currentReconnectCount > lastReconnectCount && config_.hls_config.enable_discontinuity_markers) {
                LOG_INFO("StreamWorker[%s] reconnect detected, writing discontinuity marker",
                        config_.stream_name.c_str());
                hlsMuxer_->writeDiscontinuity();
                lastReconnectCount = currentReconnectCount;
            }

            // Process video packets only
            if (packet->stream_index == rtspSource_->videoStreamIndex()) {
                // Normalize timestamps
                packetClock_->normalizePacket(packet);

                // Write to HLS muxer
                if (!hlsMuxer_->writePacket(packet, rtspSource_->videoStream())) {
                    LOG_ERROR("StreamWorker[%s] muxer failed to write packet",
                             config_.stream_name.c_str());
                    break;
                }

                health_.recordPacketWritten();
                totalBytesWritten += packet->size;
                health_.setTotalBytesWritten(totalBytesWritten);

                // Update last frame info
                int64_t now_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
                health_.setLastFrameInfo(packet->pts, now_ms);

                ++videoPacketCount;
                if (videoPacketCount % logInterval == 0) {
                    LOG_INFO("StreamWorker[%s] processed %ld video packets (segments=%d)",
                            config_.stream_name.c_str(), videoPacketCount,
                            hlsMuxer_->segmentCount());
                }
            }

        } catch (const std::exception& e) {
            LOG_ERROR("StreamWorker[%s] exception in packet processing: %s",
                     config_.stream_name.c_str(), e.what());
            lastError_ = e.what();
            atomicStatus_.store(Status::ERROR);
            break;
        }
    }

    av_packet_free(&packet);
    LOG_INFO("StreamWorker[%s] processing thread finished (processed %ld packets)",
            config_.stream_name.c_str(), videoPacketCount);
}

}  // namespace streamer
