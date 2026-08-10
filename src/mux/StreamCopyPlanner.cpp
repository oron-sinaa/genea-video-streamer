#include "streamer/StreamCopyPlanner.h"

#include "streamer/Logger.h"

namespace streamer {

StreamCopyResult StreamCopyPlanner::canCopyToMpegTs(const AVStream* sourceStream) {
    if (sourceStream == nullptr) {
        return {false, "source stream is null"};
    }

    const AVCodecParameters* params = sourceStream->codecpar;
    if (params == nullptr) {
        return {false, "codec parameters are null"};
    }

    if (!isCodecMpegTsCompatible(params->codec_id)) {
        char codecNameBuf[128] = {0};
        const char* codecName = avcodec_get_name(params->codec_id);
        return {false, "codec '" + std::string(codecName ? codecName : "unknown") + "' is not compatible with MPEG-TS container"};
    }

    if (!isCodecParamsValid(params)) {
        return {false, "codec parameters are invalid or incomplete (e.g., missing width/height)"};
    }

    return {true, ""};
}

bool StreamCopyPlanner::isCodecMpegTsCompatible(AVCodecID codecId) {
    // MPEG-TS container supports these codecs natively.
    // See: https://en.wikipedia.org/wiki/MPEG_transport_stream#Media_types
    switch (codecId) {
        // Video codecs
        case AV_CODEC_ID_MPEG2VIDEO:
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_HEVC:
        // Audio codecs
        case AV_CODEC_ID_MP2:
        case AV_CODEC_ID_MP3:
        case AV_CODEC_ID_AAC:
        case AV_CODEC_ID_AC3:
        case AV_CODEC_ID_EAC3:
        // Subtitle codecs (if ever needed)
        case AV_CODEC_ID_DVB_SUBTITLE:
            return true;
        default:
            return false;
    }
}

bool StreamCopyPlanner::isCodecParamsValid(const AVCodecParameters* params) {
    if (params == nullptr) {
        return false;
    }

    // For video, require width and height.
    if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (params->width <= 0 || params->height <= 0) {
            return false;
        }
        // Codec-specific validation: H.264 should have extradata (SPS/PPS).
        // However, some RTSP sources may have it in first packet instead, so
        // we don't strictly require it here. Log a warning if missing.
        if (params->codec_id == AV_CODEC_ID_H264 && params->extradata_size == 0) {
            LOG_WARN("H.264 stream has no extradata (SPS/PPS); may not mux correctly");
        }
        return true;
    }

    // For audio, require sample_rate.
    if (params->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (params->sample_rate <= 0) {
            return false;
        }
        return true;
    }

    // Other codec types (subtitles, etc.) are allowed but not prioritized.
    return true;
}

}  // namespace streamer
