#include "streamer/RtspSource.h"

#include "streamer/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <utility>

namespace streamer {

namespace {

std::string avErrorToString(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

}  // namespace

RtspSource::RtspSource(RtspConfig config) : config_(std::move(config)) {}

RtspSource::~RtspSource() {
    close();
}

bool RtspSource::open() {
    if (formatContext_ != nullptr) {
        LOG_WARN("RtspSource::open() called while already open; ignoring");
        return true;
    }

    if (config_.url.empty()) {
        LOG_ERROR("RtspSource: cannot open - empty RTSP url in configuration");
        return false;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", config_.transport.c_str(), 0);
    // Option name differs across LibAV versions/protocols; set both so at
    // least one is honored. Unrecognized entries are silently ignored by
    // avformat_open_input rather than causing failure.
    av_dict_set_int(&options, "stimeout", config_.timeout_us, 0);
    av_dict_set_int(&options, "timeout", config_.timeout_us, 0);

    formatContext_ = avformat_alloc_context();
    if (formatContext_ == nullptr) {
        LOG_ERROR("RtspSource: failed to allocate AVFormatContext");
        av_dict_free(&options);
        return false;
    }

    const int openRet = avformat_open_input(&formatContext_, config_.url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (openRet < 0) {
        LOG_ERROR(
            "RtspSource: failed to open RTSP source '%s': %s",
            config_.url.c_str(),
            avErrorToString(openRet).c_str());
        // avformat_open_input frees the context and nulls the pointer on failure.
        formatContext_ = nullptr;
        return false;
    }

    const int probeRet = avformat_find_stream_info(formatContext_, nullptr);
    if (probeRet < 0) {
        LOG_ERROR(
            "RtspSource: failed to read stream info from '%s': %s",
            config_.url.c_str(),
            avErrorToString(probeRet).c_str());
        close();
        return false;
    }

    videoStreamIndex_ = av_find_best_stream(formatContext_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) {
        LOG_ERROR("RtspSource: no video stream found in '%s'", config_.url.c_str());
        close();
        return false;
    }

    LOG_INFO(
        "RtspSource: opened '%s' (transport=%s, timeout_us=%d)",
        config_.url.c_str(),
        config_.transport.c_str(),
        config_.timeout_us);
    logStreamInfo();
    return true;
}

bool RtspSource::readPacket(AVPacket* packet) {
    if (!isOpen()) {
        LOG_ERROR("RtspSource::readPacket() called before open() or after close()");
        return false;
    }
    if (packet == nullptr) {
        LOG_ERROR("RtspSource::readPacket() called with null packet");
        return false;
    }

    const int ret = av_read_frame(formatContext_, packet);
    if (ret == AVERROR_EOF) {
        LOG_INFO("RtspSource: end of stream reached");
        return false;
    }
    if (ret < 0) {
        LOG_ERROR("RtspSource: error reading packet: %s", avErrorToString(ret).c_str());
        return false;
    }

    return true;
}

void RtspSource::close() {
    if (formatContext_ != nullptr) {
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
    }
    videoStreamIndex_ = -1;
}

bool RtspSource::isOpen() const {
    return formatContext_ != nullptr;
}

void RtspSource::logStreamInfo() const {
    if (!isOpen() || videoStreamIndex_ < 0) {
        return;
    }

    const AVStream* stream = formatContext_->streams[videoStreamIndex_];
    const AVCodecParameters* params = stream->codecpar;
    const AVCodecDescriptor* codecDesc = avcodec_descriptor_get(params->codec_id);

    double fps = 0.0;
    if (stream->avg_frame_rate.den != 0) {
        fps = av_q2d(stream->avg_frame_rate);
    }

    LOG_INFO(
        "RtspSource: stream #%d codec=%s resolution=%dx%d fps=%.2f time_base=%d/%d",
        videoStreamIndex_,
        codecDesc != nullptr ? codecDesc->name : "unknown",
        params->width,
        params->height,
        fps,
        stream->time_base.num,
        stream->time_base.den);
}

}  // namespace streamer
