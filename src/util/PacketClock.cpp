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

    // First, rescale DTS if present (needed for PTS generation logic below).
    if (packet->dts != AV_NOPTS_VALUE) {
        packet->dts = av_rescale_q(packet->dts, sourceTimeBase, outTimeBase);
    }

    // Handle PTS: rescale if present, generate synthetic if missing.
    if (packet->pts != AV_NOPTS_VALUE) {
        // Rescale existing PTS from source time base to output time base.
        packet->pts = av_rescale_q(packet->pts, sourceTimeBase, outTimeBase);
    } else {
        // Generate synthetic PTS if missing.
        // If DTS is already set, use it as the starting point (PTS >= DTS is required).
        // Otherwise, assume ~30 fps by default: each packet = 3000 units (at 90kHz time base).
        const int64_t DEFAULT_FRAME_DURATION = 3000;  // 30 fps at 90kHz
        
        if (packet->dts != AV_NOPTS_VALUE) {
            // DTS exists; start PTS from DTS
            packet->pts = packet->dts;
        } else if (lastPts_ != AV_NOPTS_VALUE) {
            // No DTS; continue from last PTS
            packet->pts = lastPts_ + DEFAULT_FRAME_DURATION;
        } else {
            // First packet with no PTS/DTS
            packet->pts = 0;
        }
        
        if (syntheticPtsCount_ <= 5) {
            LOG_INFO("PacketClock: Generated synthetic PTS=%lld (DTS=%s) (count=%d)", 
                    static_cast<long long>(packet->pts),
                    (packet->dts != AV_NOPTS_VALUE) ? "rescaled" : "none",
                    syntheticPtsCount_);
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

    // Handle DTS: if still missing after rescaling, generate synthetic (set to PTS).
    if (packet->dts == AV_NOPTS_VALUE) {
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
    
    // Final validation: ensure PTS >= DTS (required by MPEGTS)
    if (packet->pts < packet->dts) {
        LOG_WARN("PacketClock: PTS < DTS detected (PTS=%lld, DTS=%lld); correcting to PTS=DTS",
                static_cast<long long>(packet->pts),
                static_cast<long long>(packet->dts));
        packet->pts = packet->dts;
    }
}

}  // namespace streamer
