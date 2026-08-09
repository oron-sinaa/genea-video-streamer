#include "streamer/RtspSource.h"

#include "streamer/Logger.h"
#include "streamer/PipelineHealth.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <utility>
#include <thread>

namespace streamer {

namespace {

std::string avErrorToString(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

}  // namespace

RtspSource::RtspSource(RtspConfig config, PipelineHealth* health)
    : config_(std::move(config)),
      health_(health),
      reconnect_policy_(config_.reconnect_initial_delay_ms,
                        config_.reconnect_max_delay_ms,
                        config_.reconnect_jitter_percent),
      last_packet_time_(std::chrono::steady_clock::now()) {}

RtspSource::~RtspSource() {
    close();
}

bool RtspSource::open() {
    if (formatContext_ != nullptr) {
        LOG_WARN("RtspSource::open() called while already open; ignoring");
        return true;
    }

    if (!openConnection()) {
        LOG_ERROR("RtspSource: failed to open RTSP connection on initial attempt");
        return false;
    }

    last_packet_time_ = std::chrono::steady_clock::now();
    return true;
}

bool RtspSource::openConnection() {
    if (formatContext_ != nullptr) {
        LOG_WARN("RtspSource::openConnection() called while already connected; ignoring");
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
        closeConnection();
        return false;
    }

    videoStreamIndex_ = av_find_best_stream(formatContext_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) {
        LOG_ERROR("RtspSource: no video stream found in '%s'", config_.url.c_str());
        closeConnection();
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

void RtspSource::closeConnection() {
    if (formatContext_ != nullptr) {
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
    }
    videoStreamIndex_ = -1;
}

bool RtspSource::readPacket(AVPacket* packet) {
    if (packet == nullptr) {
        LOG_ERROR("RtspSource::readPacket() called with null packet");
        return false;
    }

    // Reconnect loop: try to read packet, reconnect on error if enabled
    while (!shutdown_requested_) {
        // Check if source is stale (no packets for configured timeout)
        if (isStale() && config_.reconnect_enabled) {
            LOG_WARN("RtspSource: no packets for %u seconds, triggering reconnect", config_.stale_timeout_s);
            closeConnection();
            if (health_) health_->recordReconnect();
        }

        // If not open, attempt to open or reconnect
        if (!isOpen()) {
            if (!config_.reconnect_enabled) {
                LOG_ERROR("RtspSource: connection closed and reconnect disabled");
                return false;
            }

            if (reconnect_policy_.shouldRetry()) {
                LOG_INFO("RtspSource: attempting reconnect (delay %u ms)",
                         reconnect_policy_.getNextDelayMs());

                if (openConnection()) {
                    reconnect_policy_.reset();
                    LOG_INFO("RtspSource: reconnection successful");
                } else {
                    reconnect_policy_.recordAttempt();
                    // Sleep before next attempt to avoid busy-wait
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
            } else {
                // Not yet time to retry; sleep and check again
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }

        // Try to read packet
        const int ret = av_read_frame(formatContext_, packet);
        
        if (ret == AVERROR_EOF) {
            LOG_INFO("RtspSource: end of stream reached");
            if (!config_.reconnect_enabled) {
                return false;
            }
            closeConnection();
            if (health_) health_->recordReconnect();
            continue;
        }
        
        if (ret < 0) {
            LOG_WARN("RtspSource: error reading packet: %s (will reconnect if enabled)", avErrorToString(ret).c_str());
            if (!config_.reconnect_enabled) {
                LOG_ERROR("RtspSource: packet read error and reconnect disabled");
                return false;
            }
            closeConnection();
            if (health_) health_->recordReconnect();
            continue;
        }

        // Packet successfully read
        last_packet_time_ = std::chrono::steady_clock::now();
        if (health_) health_->recordPacketRead();
        return true;
    }

    // Shutdown was requested
    LOG_INFO("RtspSource: shutdown requested, stopping read loop");
    return false;
}

bool RtspSource::isStale() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_packet_time_).count();
    return elapsed > config_.stale_timeout_s;
}

void RtspSource::close() {
    shutdown_requested_ = true;
    closeConnection();
}

bool RtspSource::isOpen() const {
    return formatContext_ != nullptr;
}

AVStream* RtspSource::videoStream() const {
    if (!isOpen() || videoStreamIndex_ < 0) {
        return nullptr;
    }
    return formatContext_->streams[videoStreamIndex_];
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
