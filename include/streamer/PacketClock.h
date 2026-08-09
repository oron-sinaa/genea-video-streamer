#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

namespace streamer {

// Normalizes packet timestamps to a target time base while enforcing
// monotonic increase (no backward jumps in PTS/DTS).
//
// Motivation: RTSP sources may have jitter, gaps, or frame reordering that
// violates the strict ordering needed for HLS playlists. This layer rescales
// timestamps and clamps them to ensure they never decrease.
//
// Time base: typically 90000 for HLS/MPEG-TS; must be the output container
// time base, not the source stream time base.
class PacketClock {
public:
    // Constructs a clock for the given source stream, normalizing all packet
    // timestamps to the provided output time base.
    //
    // Precondition: `stream` must be valid (typically from AVFormatContext->streams[index]).
    // Postcondition: Call normalizePacket() on each packet before remuxing.
    PacketClock(const AVStream* sourceStream, int outputTimeBase);

    ~PacketClock() = default;

    // Noncopyable (contains state that tracks last PTS/DTS).
    PacketClock(const PacketClock&) = delete;
    PacketClock& operator=(const PacketClock&) = delete;

    // Rescales the packet's PTS and DTS to the output time base and enforces
    // monotonic increase. On return, `packet->pts` and `packet->dts` are updated
    // in-place to reflect the normalized timestamps.
    //
    // If jitter or backward motion is detected, a warning is logged but processing
    // continues (clamping to last seen timestamp + 1 to preserve ordering).
    void normalizePacket(AVPacket* packet);

    // Returns the last normalized PTS, or AV_NOPTS_VALUE if no packets processed yet.
    int64_t lastPts() const { return lastPts_; }

    // Returns the last normalized DTS, or AV_NOPTS_VALUE if no packets processed yet.
    int64_t lastDts() const { return lastDts_; }

private:
    int sourceTimeBaseDen_ = 1;  // source time base denominator (e.g., 1000 for 1/1000)
    int sourceTimeBaseNum_ = 1;  // source time base numerator
    int outputTimeBase_ = 90000; // output time base (e.g., 90000 for HLS)

    int64_t lastPts_ = AV_NOPTS_VALUE;
    int64_t lastDts_ = AV_NOPTS_VALUE;

    int ptsJitterCount_ = 0;  // number of backward PTS detections (for stats)
    int dtsJitterCount_ = 0;  // number of backward DTS detections (for stats)
};

}  // namespace streamer
