#pragma once

#include "streamer/Config.h"
#include "streamer/VideoSource.h"

namespace streamer {

// Captures compressed packets from an IP camera over RTSP using LibAV.
// Selects the first video stream found and exposes it via readPacket().
class RtspSource : public IVideoSource {
public:
    explicit RtspSource(RtspConfig config);
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

private:
    RtspConfig config_;
    AVFormatContext* formatContext_ = nullptr;
    int videoStreamIndex_ = -1;
};

}  // namespace streamer
