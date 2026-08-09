#pragma once

#include "streamer/Config.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <ctime>
#include <string>
#include <vector>

namespace streamer {

// Segment metadata for tracking and playlist generation.
struct SegmentInfo {
    std::string filename;    // e.g., "segment_000001.ts"
    int64_t startPts = 0;    // PTS of first packet (in output time base)
    int64_t endPts = 0;      // PTS of last packet (in output time base)
    double duration = 0.0;   // calculated duration in seconds
    std::time_t createdAt = 0;
};

// Muxes compressed packets into HLS-compatible MPEG-TS segments.
//
// Responsibilities:
//  1. Create and manage .ts segment files
//  2. Write packets to segments, rotate when duration limit reached
//  3. Generate playlists: live.m3u8 (rolling window) + archive.m3u8 (VOD)
//  4. Cleanup old segments beyond retention policy
//  5. Track segment metadata for playlist generation
class HlsMuxer {
public:
    explicit HlsMuxer(const HlsConfig& config);
    ~HlsMuxer();

    // Noncopyable (owns file handles and state).
    HlsMuxer(const HlsMuxer&) = delete;
    HlsMuxer& operator=(const HlsMuxer&) = delete;

    // Initializes output directory and internal state. Must be called before
    // writing packets. Returns true on success.
    bool open();

    // Writes a packet to the current segment. If packet duration would exceed
    // segment target, a new segment is started first. Returns true on success.
    //
    // Precondition: `packet` has been normalized by PacketClock (monotonic PTS/DTS).
    // Precondition: open() has been called successfully.
    bool writePacket(const AVPacket* packet, const AVStream* sourceStream);

    // Finalizes the current segment, updates playlists, and closes file handles.
    // Safe to call multiple times.
    void close();

    // Returns true if the muxer is currently open.
    bool isOpen() const { return opened_; }

    // Returns the total number of packets written.
    long packetCount() const { return packetCount_; }

    // Returns the number of segments created.
    int segmentCount() const { return static_cast<int>(segments_.size()); }

private:
    // Creates a new segment file and initializes muxing context for it.
    // Called when current segment would exceed duration limit.
    bool startNewSegment();

    // Closes the current segment file and records its metadata.
    bool closeCurrentSegment();

    // Updates live.m3u8 with rolling window of most recent segments.
    // Live window contains last 3-5 segments to enable smooth playback.
    bool updateLivePlaylist();

    // Updates archive.m3u8 with all segments for VOD playback and seeking.
    bool updateArchivePlaylist();

    // Scans segments directory and removes .ts files older than retention policy.
    void cleanupOldSegments();

    // Writes playlist content to a file atomically (write temp, rename).
    bool writePlaylistFile(const std::string& filename, const std::string& content);

    // Generates the M3U8 playlist content for given segment list.
    std::string generatePlaylistContent(
        const std::vector<SegmentInfo>& segments,
        bool isLive,
        int outputTimeBase);

    HlsConfig config_;
    bool opened_ = false;

    // Output directory path (absolute or relative).
    std::string outputDir_;

    // Current segment state.
    AVFormatContext* currentSegmentFormat_ = nullptr;
    int currentSegmentIndex_ = 0;
    int64_t currentSegmentStartPts_ = 0;
    int64_t lastPacketPts_ = 0;
    std::string currentSegmentFilename_;

    // All segments created so far.
    std::vector<SegmentInfo> segments_;

    // Metrics and state.
    long packetCount_ = 0;
    std::time_t lastCleanupTime_ = 0;

    // Output stream codec info (saved at open time for playlist generation).
    int outputTimeBase_ = 90000;  // standard for HLS/MPEG-TS
    int sourceTimeBaseDen_ = 1;
    int sourceTimeBaseNum_ = 1;
};

}  // namespace streamer
