#include "streamer/PacketClock.h"

#include "streamer/Logger.h"

extern "C" {
#include <libavutil/mathematics.h>
}

namespace streamer {

PacketClock::PacketClock(const AVStream* sourceStream, int outputTimeBase)
    : outputTimeBase_(outputTimeBase) {
    if (sourceStream != nullptr && sourceStream->time_base.den > 0) {
        sourceTimeBaseNum_ = sourceStream->time_base.num;
        sourceTimeBaseDen_ = sourceStream->time_base.den;
    }
}

void PacketClock::normalizePacket(AVPacket* packet) {
    if (packet == nullptr) {
        LOG_WARN("PacketClock::normalizePacket() called with null packet");
        return;
    }

    const AVRational sourceTimeBase{sourceTimeBaseNum_, sourceTimeBaseDen_};
    const AVRational outTimeBase{1, outputTimeBase_};

    // Handle PTS: rescale if present, generate synthetic if missing.
    if (packet->pts != AV_NOPTS_VALUE) {
        // Rescale existing PTS from source time base to output time base.
        packet->pts = av_rescale_q(packet->pts, sourceTimeBase, outTimeBase);
    } else {
        // Generate synthetic PTS if missing.
        // Assume ~30 fps by default: each packet = 3000 units (at 90kHz time base).
        const int64_t DEFAULT_FRAME_DURATION = 3000;  // 30 fps at 90kHz
        if (lastPts_ == AV_NOPTS_VALUE) {
            packet->pts = 0;
        } else {
            packet->pts = lastPts_ + DEFAULT_FRAME_DURATION;
        }
        if (syntheticPtsCount_ <= 5) {
            LOG_INFO("PacketClock: Generated synthetic PTS=%lld (count=%d)", 
                    static_cast<long long>(packet->pts), syntheticPtsCount_);
            ++syntheticPtsCount_;
        }
    }

    // Enforce monotonic PTS: clamp to at least lastPts_ + 1 to preserve order.
    if (lastPts_ != AV_NOPTS_VALUE && packet->pts <= lastPts_) {
        ++ptsJitterCount_;
        if (ptsJitterCount_ <= 10) {  // Log first 10 jitters to avoid spam.
            LOG_WARN(
                "PacketClock: PTS jitter detected: %lld <= %lld (count=%d)",
                static_cast<long long>(packet->pts),
                static_cast<long long>(lastPts_),
                ptsJitterCount_);
        }
        packet->pts = lastPts_ + 1;
    }
    lastPts_ = packet->pts;

    // Handle DTS: rescale if present, generate synthetic if missing.
    if (packet->dts != AV_NOPTS_VALUE) {
        // Rescale existing DTS from source time base to output time base.
        packet->dts = av_rescale_q(packet->dts, sourceTimeBase, outTimeBase);
    } else {
        // Generate synthetic DTS if missing (typically same or slightly before PTS).
        packet->dts = packet->pts;
    }

    // Enforce monotonic DTS: clamp to at least lastDts_ + 1 to preserve order.
    if (lastDts_ != AV_NOPTS_VALUE && packet->dts <= lastDts_) {
        ++dtsJitterCount_;
        if (dtsJitterCount_ <= 10) {  // Log first 10 jitters to avoid spam.
            LOG_WARN(
                "PacketClock: DTS jitter detected: %lld <= %lld (count=%d)",
                static_cast<long long>(packet->dts),
                static_cast<long long>(lastDts_),
                dtsJitterCount_);
        }
        packet->dts = lastDts_ + 1;
    }
    lastDts_ = packet->dts;
}

}  // namespace streamer
