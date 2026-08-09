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

    // Rescale PTS from source time base to output time base.
    // av_rescale_q handles AV_NOPTS_VALUE gracefully (returns AV_NOPTS_VALUE).
    if (packet->pts != AV_NOPTS_VALUE) {
        const AVRational sourceTimeBase{sourceTimeBaseNum_, sourceTimeBaseDen_};
        const AVRational outTimeBase{1, outputTimeBase_};
        packet->pts = av_rescale_q(packet->pts, sourceTimeBase, outTimeBase);

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
    }

    // Rescale DTS from source time base to output time base.
    if (packet->dts != AV_NOPTS_VALUE) {
        const AVRational sourceTimeBase{sourceTimeBaseNum_, sourceTimeBaseDen_};
        const AVRational outTimeBase{1, outputTimeBase_};
        packet->dts = av_rescale_q(packet->dts, sourceTimeBase, outTimeBase);

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
}

}  // namespace streamer
