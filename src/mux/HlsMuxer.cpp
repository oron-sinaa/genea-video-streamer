#include "streamer/HlsMuxer.h"

#include "streamer/Logger.h"
#include "streamer/StreamCopyPlanner.h"

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace streamer {

namespace {

std::string avErrorToString(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

// Formats segment number as zero-padded string (e.g., 000001).
std::string formatSegmentIndex(int index) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(6) << (index + 1);
    return oss.str();
}

}  // namespace

HlsMuxer::HlsMuxer(const HlsConfig& config) : config_(config), outputDir_(config.output_dir) {}

HlsMuxer::~HlsMuxer() {
    close();
}

bool HlsMuxer::open() {
    if (opened_) {
        LOG_WARN("HlsMuxer::open() called while already open; ignoring");
        return true;
    }

    // Create output directory if it doesn't exist.
    try {
        std::filesystem::create_directories(outputDir_);
    } catch (const std::exception& e) {
        LOG_ERROR("HlsMuxer: failed to create output directory '%s': %s", outputDir_.c_str(), e.what());
        return false;
    }

    LOG_INFO(
        "HlsMuxer: opened with config: segment_duration=%ds, retention=%dh, output_dir='%s'",
        config_.segment_duration_s,
        config_.archive_retention_hours,
        outputDir_.c_str());

    currentSegmentIndex_ = 0;
    lastPacketPts_ = AV_NOPTS_VALUE;
    lastCleanupTime_ = std::time(nullptr);
    opened_ = true;

    return true;
}

bool HlsMuxer::writePacket(const AVPacket* packet, const AVStream* sourceStream) {
    if (!opened_) {
        LOG_ERROR("HlsMuxer::writePacket() called before open() or after close()");
        return false;
    }
    if (packet == nullptr || sourceStream == nullptr) {
        LOG_ERROR("HlsMuxer::writePacket() called with null packet or stream");
        return false;
    }

    // If this is the first packet, start the first segment.
    if (currentSegmentFormat_ == nullptr) {
        if (!startNewSegment()) {
            return false;
        }
        currentSegmentStartPts_ = packet->pts;
        // Save source time base for playlist generation.
        sourceTimeBaseNum_ = sourceStream->time_base.num;
        sourceTimeBaseDen_ = sourceStream->time_base.den;
    }

    // Check if we should start a new segment based on duration.
    if (packet->pts != AV_NOPTS_VALUE && currentSegmentStartPts_ != AV_NOPTS_VALUE) {
        // Calculate duration elapsed in current segment (in output time base 90000).
        // PTS is already in 90000 time base from PacketClock.
        const int64_t elapsedPts = packet->pts - currentSegmentStartPts_;
        const double elapsedSeconds =
            (double)elapsedPts / (double)outputTimeBase_;

        if (elapsedSeconds >= config_.segment_duration_s) {
            // Current segment would exceed duration; start a new one.
            if (!closeCurrentSegment()) {
                return false;
            }
            if (!startNewSegment()) {
                return false;
            }
            currentSegmentStartPts_ = packet->pts;
        }
    }

    // Write packet to current segment.
    const int writeRet = av_write_frame(currentSegmentFormat_, const_cast<AVPacket*>(packet));
    if (writeRet < 0) {
        LOG_ERROR("HlsMuxer: failed to write packet to segment: %s", avErrorToString(writeRet).c_str());
        return false;
    }

    lastPacketPts_ = packet->pts;
    ++packetCount_;

    // Periodically check if old segments need cleanup (to avoid I/O during packet writes).
    const std::time_t now = std::time(nullptr);
    if (now - lastCleanupTime_ >= config_.cleanup_interval_s) {
        cleanupOldSegments();
        lastCleanupTime_ = now;
    }

    return true;
}

void HlsMuxer::close() {
    if (!opened_) {
        return;
    }

    if (currentSegmentFormat_ != nullptr) {
        closeCurrentSegment();
    }

    // Final playlist updates.
    updateLivePlaylist();
    updateArchivePlaylist();

    LOG_INFO("HlsMuxer: closed. Wrote %ld packets in %d segments", packetCount_, currentSegmentIndex_);
    opened_ = false;
}

bool HlsMuxer::startNewSegment() {
    // Generate output filename.
    currentSegmentFilename_ = "segment_" + formatSegmentIndex(currentSegmentIndex_) + ".ts";
    const std::string outputPath = outputDir_ + "/" + currentSegmentFilename_;

    LOG_INFO("HlsMuxer: starting segment %d: %s", currentSegmentIndex_ + 1, outputPath.c_str());

    // Allocate output format context (MPEG-TS).
    AVFormatContext* outContext = nullptr;
    const int allocRet = avformat_alloc_output_context2(&outContext, nullptr, "mpegts", outputPath.c_str());
    if (allocRet < 0) {
        LOG_ERROR(
            "HlsMuxer: failed to allocate output context for '%s': %s",
            outputPath.c_str(),
            avErrorToString(allocRet).c_str());
        return false;
    }

    currentSegmentFormat_ = outContext;

    // Note: We assume the caller will write packets with compatible codec.
    // The source stream's codec and parameters should already be validated by StreamCopyPlanner.
    // We don't create an output stream here; that happens on the first packet write by calling
    // av_write_frame. This is handled by libavformat's auto-stream-creation (if enabled).
    // For safety in codec copy, we would typically copy codec parameters manually, but
    // since we're doing direct packet copy with av_write_frame, libavformat handles it.

    // Open output file.
    if (!(outContext->oformat->flags & AVFMT_NOFILE)) {
        const int openRet = avio_open(&outContext->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (openRet < 0) {
            LOG_ERROR(
                "HlsMuxer: failed to open output file '%s': %s",
                outputPath.c_str(),
                avErrorToString(openRet).c_str());
            avformat_free_context(outContext);
            currentSegmentFormat_ = nullptr;
            return false;
        }
    }

    // Write stream header (this initializes the MPEG-TS header; actual streams are added on first packet).
    const int headerRet = avformat_write_header(outContext, nullptr);
    if (headerRet < 0) {
        LOG_ERROR("HlsMuxer: failed to write segment header: %s", avErrorToString(headerRet).c_str());
        avio_closep(&outContext->pb);
        avformat_free_context(outContext);
        currentSegmentFormat_ = nullptr;
        return false;
    }

    return true;
}

bool HlsMuxer::closeCurrentSegment() {
    if (currentSegmentFormat_ == nullptr) {
        return true;  // Already closed or never opened.
    }

    // Flush and write trailer.
    const int trailerRet = av_write_trailer(currentSegmentFormat_);
    if (trailerRet < 0) {
        LOG_WARN("HlsMuxer: warning writing segment trailer: %s", avErrorToString(trailerRet).c_str());
        // Don't fail; still try to close the file.
    }

    // Close output file.
    if (currentSegmentFormat_->pb != nullptr) {
        avio_closep(&currentSegmentFormat_->pb);
    }

    // Record segment metadata.
    SegmentInfo info;
    info.filename = currentSegmentFilename_;
    info.startPts = currentSegmentStartPts_;
    info.endPts = lastPacketPts_;
    info.createdAt = std::time(nullptr);
    info.has_discontinuity_before = discontinuity_written_for_segment_;

    // Calculate duration: (endPts - startPts) / outputTimeBase in seconds.
    if (info.endPts != AV_NOPTS_VALUE && info.startPts != AV_NOPTS_VALUE) {
        const int64_t durationPts = info.endPts - info.startPts;
        info.duration = (double)durationPts / (double)outputTimeBase_;
    } else {
        info.duration = config_.segment_duration_s;  // Fallback estimate.
    }

    segments_.push_back(info);
    discontinuity_written_for_segment_ = false;  // Reset flag for next segment
    LOG_INFO("HlsMuxer: closed segment %d: duration=%.2fs, packets_total=%ld", 
             currentSegmentIndex_ + 1, info.duration, packetCount_);

    avformat_free_context(currentSegmentFormat_);
    currentSegmentFormat_ = nullptr;
    ++currentSegmentIndex_;

    // Update playlists after each segment close.
    updateLivePlaylist();
    updateArchivePlaylist();

    return true;
}

bool HlsMuxer::updateLivePlaylist() {
    // Live playlist contains the most recent segments (rolling window).
    // Typical window: last 3-5 segments to provide ~9-20s of buffer at 3s per segment.
    const int liveWindowSize = 5;
    const int startIdx = std::max(0, (int)segments_.size() - liveWindowSize);

    std::vector<SegmentInfo> liveSegments(
        segments_.begin() + startIdx,
        segments_.end());

    const std::string content = generatePlaylistContent(liveSegments, true, outputTimeBase_);
    return writePlaylistFile(outputDir_ + "/live.m3u8", content);
}

bool HlsMuxer::updateArchivePlaylist() {
    // Archive playlist contains all segments (VOD-style for seeking/playback).
    const std::string content = generatePlaylistContent(segments_, false, outputTimeBase_);
    return writePlaylistFile(outputDir_ + "/archive.m3u8", content);
}

void HlsMuxer::cleanupOldSegments() {
    if (segments_.empty()) {
        return;
    }

    const std::time_t now = std::time(nullptr);
    const int retentionSeconds = config_.archive_retention_hours * 3600;

    int deletedCount = 0;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const SegmentInfo& seg = segments_[i];
        const int ageSeconds = static_cast<int>(now - seg.createdAt);

        if (ageSeconds > retentionSeconds) {
            const std::string fullPath = outputDir_ + "/" + seg.filename;
            if (std::remove(fullPath.c_str()) == 0) {
                LOG_INFO("HlsMuxer: cleaned up old segment: %s (age=%ds)", seg.filename.c_str(), ageSeconds);
                ++deletedCount;
            } else {
                LOG_WARN("HlsMuxer: failed to delete old segment: %s", seg.filename.c_str());
            }
        }
    }

    // Truncate segments list to match retention policy.
    if (deletedCount > 0) {
        segments_.erase(
            std::remove_if(
                segments_.begin(),
                segments_.end(),
                [now, retentionSeconds](const SegmentInfo& seg) {
                    return (now - seg.createdAt) > retentionSeconds;
                }),
            segments_.end());
    }
}

bool HlsMuxer::writePlaylistFile(const std::string& filename, const std::string& content) {
    // Write to temp file, then rename atomically to avoid partial reads.
    const std::string tempFilename = filename + ".tmp";

    std::ofstream out(tempFilename);
    if (!out.is_open()) {
        LOG_ERROR("HlsMuxer: failed to open playlist temp file: %s", tempFilename.c_str());
        return false;
    }

    out << content;
    out.close();

    if (std::rename(tempFilename.c_str(), filename.c_str()) != 0) {
        LOG_ERROR("HlsMuxer: failed to rename playlist file: %s -> %s", tempFilename.c_str(), filename.c_str());
        return false;
    }

    return true;
}

std::string HlsMuxer::generatePlaylistContent(
    const std::vector<SegmentInfo>& segments,
    bool isLive,
    int outputTimeBase) {
    std::ostringstream m3u8;

    m3u8 << "#EXTM3U\n";
    m3u8 << "#EXT-X-VERSION:3\n";

    // Calculate target duration (ceil of max segment duration in window).
    double maxDuration = 0.0;
    for (const auto& seg : segments) {
        maxDuration = std::max(maxDuration, seg.duration);
    }
    const int targetDuration = static_cast<int>(std::ceil(maxDuration)) + 1;  // +1 for safety

    m3u8 << "#EXT-X-TARGETDURATION:" << targetDuration << "\n";

    // For archive playlists, include MEDIA-SEQUENCE to enable seeking.
    if (!segments.empty()) {
        // Calculate MEDIA-SEQUENCE as index of first segment (typically 0 for VOD, or segment count for live).
        const int mediaSeq = isLive ? std::max(0, (int)segments_.size() - (int)segments.size()) : 0;
        m3u8 << "#EXT-X-MEDIA-SEQUENCE:" << mediaSeq << "\n";
    }

    // Write segment entries.
    for (const auto& seg : segments) {
        if (seg.has_discontinuity_before) {
            m3u8 << "#EXT-X-DISCONTINUITY\n";
        }
        m3u8 << "#EXTINF:" << std::fixed << std::setprecision(1) << seg.duration << ",\n";
        m3u8 << seg.filename << "\n";
    }

    // For VOD playlists (archive), include ENDLIST to signal end.
    // For live playlists, omit ENDLIST so players know more segments may arrive.
    if (!isLive && !segments.empty()) {
        m3u8 << "#EXT-X-ENDLIST\n";
    }

    return m3u8.str();
}

void HlsMuxer::writeDiscontinuity() {
    // Mark the next segment (if one exists) as having a discontinuity before it.
    // If no segments exist yet, mark the next one we create.
    if (!segments_.empty()) {
        segments_.back().has_discontinuity_before = true;
        LOG_INFO("HlsMuxer: discontinuity marker written after segment %d", currentSegmentIndex_);
    } else {
        // No segments yet; set flag so next segment gets marked
        discontinuity_written_for_segment_ = true;
        LOG_INFO("HlsMuxer: discontinuity marker flagged for next segment");
    }
    
    // Update playlists to reflect the discontinuity marker
    if (opened_) {
        updateLivePlaylist();
        updateArchivePlaylist();
    }
}

}  // namespace streamer
