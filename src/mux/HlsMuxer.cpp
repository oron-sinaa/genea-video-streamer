#include "streamer/HlsMuxer.h"

#include "streamer/Logger.h"
#include "streamer/StreamCopyPlanner.h"

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <cerrno>
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

    // Recover segment index from existing segment files in directory.
    // This allows segment numbering to continue across stream restarts.
    int maxExistingIndex = -1;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(outputDir_)) {
            if (entry.is_regular_file()) {
                const std::string filename = entry.path().filename().string();
                // Match pattern: segment_XXXXXX.ts
                if (filename.find("segment_") == 0 && filename.find(".ts") != std::string::npos) {
                    try {
                        // Extract the number portion (segment_XXXXXX.ts)
                        size_t underscore_pos = filename.find('_');
                        size_t dot_pos = filename.rfind('.');
                        if (underscore_pos != std::string::npos && dot_pos != std::string::npos) {
                            const std::string numStr = filename.substr(underscore_pos + 1, dot_pos - underscore_pos - 1);
                            int segmentNum = std::stoi(numStr);
                            maxExistingIndex = std::max(maxExistingIndex, segmentNum - 1);  // Convert to 0-based index
                        }
                    } catch (const std::exception&) {
                        // Ignore parse errors; just continue scanning
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("HlsMuxer: failed to scan existing segments: %s", e.what());
        // Not fatal; just start from 0
    }

    // Set currentSegmentIndex_ to resume from last segment (or 0 if none exist)
    currentSegmentIndex_ = maxExistingIndex + 1;
    if (maxExistingIndex >= 0) {
        LOG_INFO("HlsMuxer: resuming segment numbering from index %d (last segment was %d)",
                 currentSegmentIndex_, maxExistingIndex);
    }

    LOG_INFO(
        "HlsMuxer: opened with config: segment_duration=%ds, retention=%dh, output_dir='%s'",
        config_.segment_duration_s,
        config_.archive_retention_hours,
        outputDir_.c_str());

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

    // If segment header not yet written, setup output stream and write header on first packet.
    if (!headerWritten_) {
        if (!setupOutputStream(sourceStream)) {
            return false;
        }
        headerWritten_ = true;
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
            // Setup output stream for new segment (headerWritten_ was reset to false in startNewSegment).
            if (!setupOutputStream(sourceStream)) {
                return false;
            }
            headerWritten_ = true;
        }
    }

    // If stream restart is pending, write discontinuity before next segment.
    if (stream_restart_pending_) {
        writeDiscontinuity();
        stream_restart_pending_ = false;
    }

    // Write packet to current segment.
    // Ensure packet has correct time base (should already be 90000 from PacketClock, but double-check).
    AVPacket* pkt = const_cast<AVPacket*>(packet);
    const int writeRet = av_write_frame(currentSegmentFormat_, pkt);
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
    
    // Build output path, avoiding double slashes.
    std::string outputPath = outputDir_;
    if (!outputPath.empty() && outputPath.back() != '/') {
        outputPath += "/";
    }
    outputPath += currentSegmentFilename_;

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

    // Defer stream creation and header writing until first packet arrives.
    // This is required because at segment start, we don't have source stream codec parameters yet.
    // The first packet write will trigger setupOutputStream() and avformat_write_header().
    headerWritten_ = false;

    return true;
}

bool HlsMuxer::setupOutputStream(const AVStream* sourceStream) {
    if (currentSegmentFormat_ == nullptr) {
        LOG_ERROR("HlsMuxer::setupOutputStream() called with null output context");
        return false;
    }
    if (sourceStream == nullptr || sourceStream->codecpar == nullptr) {
        LOG_ERROR("HlsMuxer::setupOutputStream() called with null source stream or codec params");
        return false;
    }

    // Create output stream with same codec type as source.
    AVStream* outStream = avformat_new_stream(currentSegmentFormat_, nullptr);
    if (outStream == nullptr) {
        LOG_ERROR("HlsMuxer: failed to create output stream");
        return false;
    }

    // Copy codec parameters from source to output stream.
    const int copyRet = avcodec_parameters_copy(outStream->codecpar, sourceStream->codecpar);
    if (copyRet < 0) {
        LOG_ERROR(
            "HlsMuxer: failed to copy codec parameters: %s",
            avErrorToString(copyRet).c_str());
        return false;
    }

    // Set output stream time base to standard HLS time base (90000 Hz).
    outStream->time_base = {1, outputTimeBase_};

    // Now write the header with the stream properly configured.
    const int headerRet = avformat_write_header(currentSegmentFormat_, nullptr);
    if (headerRet < 0) {
        LOG_ERROR("HlsMuxer: failed to write segment header: %s", avErrorToString(headerRet).c_str());
        return false;
    }

    LOG_INFO("HlsMuxer: wrote segment header with codec=%s", av_get_media_type_string(sourceStream->codecpar->codec_type));
    return true;
}

bool HlsMuxer::closeCurrentSegment() {
    if (currentSegmentFormat_ == nullptr) {
        return true;  // Already closed or never opened.
    }

    // Flush and write trailer only if header was written (i.e., at least one packet was written).
    if (headerWritten_) {
        const int trailerRet = av_write_trailer(currentSegmentFormat_);
        if (trailerRet < 0) {
            LOG_WARN("HlsMuxer: warning writing segment trailer: %s", avErrorToString(trailerRet).c_str());
            // Don't fail; still try to close the file.
        }
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

bool HlsMuxer::cleanupOldSegments() {
    if (segments_.empty()) {
        return false;  // No segments to clean
    }

    const std::time_t now = std::time(nullptr);
    const int retentionSeconds = config_.archive_retention_hours * 3600;
    
    LOG_INFO("HlsMuxer: cleanup check started. Current segments: %zu, retention: %dh (%ds), age threshold: 2h (7200s)",
             segments_.size(), config_.archive_retention_hours, retentionSeconds);

    int deletedCount = 0;
    int failedCount = 0;
    std::vector<std::string> deletedFilenames;

    // First pass: attempt to delete old segment files
    for (const auto& seg : segments_) {
        const int ageSeconds = static_cast<int>(now - seg.createdAt);
        
        // Only delete segments older than 2 hours (7200 seconds) OR older than retention policy
        if (ageSeconds > retentionSeconds && ageSeconds > 7200) {
            const std::string fullPath = outputDir_ + "/" + seg.filename;
            
            // Verify file exists before attempting deletion
            if (!std::filesystem::exists(fullPath)) {
                LOG_WARN("HlsMuxer: segment file already missing: %s (age=%ds)", seg.filename.c_str(), ageSeconds);
                deletedCount++;
                deletedFilenames.push_back(seg.filename);
                continue;
            }
            
            // Attempt deletion
            if (std::remove(fullPath.c_str()) == 0) {
                LOG_INFO("HlsMuxer: DELETED segment: %s (age=%ds, retention=%ds)", 
                         seg.filename.c_str(), ageSeconds, retentionSeconds);
                deletedCount++;
                deletedFilenames.push_back(seg.filename);
            } else {
                LOG_ERROR("HlsMuxer: FAILED to delete segment: %s (age=%ds) - %s", 
                          seg.filename.c_str(), ageSeconds, std::strerror(errno));
                failedCount++;
            }
        }
    }

    // Second pass: remove deleted segments from tracking vector
    // Only remove if deletion was successful (file doesn't exist anymore)
    if (deletedCount > 0) {
        auto it = segments_.begin();
        while (it != segments_.end()) {
            const std::string fullPath = outputDir_ + "/" + it->filename;
            
            // If file was deleted (doesn't exist), remove from tracking
            if (!std::filesystem::exists(fullPath)) {
                it = segments_.erase(it);
            } else {
                ++it;
            }
        }
        
        LOG_INFO("HlsMuxer: cleanup removed %zu entries from segments_ tracking vector", deletedCount);
    }

    // Third pass: regenerate playlists only if segments were deleted
    if (deletedCount > 0 || failedCount > 0) {
        LOG_INFO("HlsMuxer: cleanup complete - DELETED: %d files (failed: %d), updating playlists...",
                 deletedCount, failedCount);
        
        // Regenerate both playlists to remove references to deleted segments
        updateLivePlaylist();
        updateArchivePlaylist();
        
        LOG_INFO("HlsMuxer: playlists regenerated after cleanup. Remaining segments: %zu", segments_.size());
        
        // Log detailed cleanup summary
        std::ostringstream summary;
        summary << "HlsMuxer: cleanup summary - deleted files: [";
        for (size_t i = 0; i < deletedFilenames.size() && i < 10; ++i) {
            if (i > 0) summary << ", ";
            summary << deletedFilenames[i];
        }
        if (deletedFilenames.size() > 10) {
            summary << ", ... (" << (deletedFilenames.size() - 10) << " more)";
        }
        summary << "]";
        LOG_DEBUG(summary.str().c_str());
        
        return true;  // Cleanup occurred
    } else {
        LOG_DEBUG("HlsMuxer: cleanup check found no segments old enough to delete");
        return false;  // No cleanup needed
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

    // NOTE: NEVER add ENDLIST to archive playlists.
    // Archive is a continuously growing rolling window, not a final VOD.
    // ENDLIST should only be added when the stream is truly stopping permanently,
    // which is handled separately (not in the normal playlist generation).
    // For live playlists, omit ENDLIST so players know more segments may arrive.

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

void HlsMuxer::handleStreamRestart() {
    // Called when source stream is reconnected or codec parameters change.
    // Signals that a discontinuity should be written to the playlist.
    // The actual discontinuity marker is written when the next segment is written.
    
    LOG_WARN("HlsMuxer: stream restart detected - discontinuity will be written at next segment");
    
    // Set flag to write discontinuity on next packet write
    stream_restart_pending_ = true;
    
    // If we're in the middle of a segment, immediately write discontinuity to playlists
    // so live players know about the discontinuity ASAP
    if (!segments_.empty()) {
        writeDiscontinuity();
    }
}

}  // namespace streamer
