#pragma once

#include "streamer/Config.h"
#include "streamer/VideoSource.h"
#include "streamer/ReconnectPolicy.h"

#include <chrono>

namespace streamer {

// Forward declaration
class PipelineHealth;

// Captures compressed packets from an IP camera over RTSP using LibAV.
// Selects the first video stream found and exposes it via readPacket().
// Automatically reconnects on network errors with exponential backoff.
class RtspSource : public IVideoSource {
public:
    explicit RtspSource(RtspConfig config, PipelineHealth* health = nullptr);
    ~RtspSource() override;

    // Owns a raw AVFormatContext*; copying would double-free on close().
    RtspSource(const RtspSource&) = delete;
    RtspSource& operator=(const RtspSource&) = delete;

    bool open() override;
    bool readPacket(AVPacket* packet) override;
    void close() override;
    bool isOpen() const override;

    // Index of the selected video stream, or -1 if not open.
    int videoStreamIndex() const { return videoStreamIndex_; }

    // Returns the AVStream* for the selected video stream, or nullptr if not open
    // or no video stream found. Caller must not retain the pointer past close().
    AVStream* videoStream() const;

    // Logs codec, resolution, fps, and time base for the selected video stream.
    // No-op if the source is not open.
    void logStreamInfo() const;

    // Request graceful shutdown (stops reconnect loop).
    void requestShutdown() { shutdown_requested_ = true; }

private:
    // Opens a new RTSP connection. Called from open() and from reconnect loop.
    bool openConnection();

    // Closes current RTSP connection. Called from close() and from reconnect loop.
    void closeConnection();

    // Check if source is stale (no packets for N seconds). Returns true if stale.
    bool isStale() const;

    RtspConfig config_;
    PipelineHealth* health_;  // pointer to shared health tracker (can be nullptr)
    AVFormatContext* formatContext_ = nullptr;
    int videoStreamIndex_ = -1;
    
    ReconnectPolicy reconnect_policy_;
    std::chrono::steady_clock::time_point last_packet_time_;
    bool shutdown_requested_ = false;
};

}  // namespace streamer
