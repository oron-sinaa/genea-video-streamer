#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

namespace streamer {

// Abstract interface for a packet-level (compressed) video source.
// Implementations do not decode frames; they hand back raw AVPackets
// for downstream remuxing (see design.md section 4.2).
class IVideoSource {
public:
    virtual ~IVideoSource() = default;

    // Opens the source and probes stream metadata. Returns true on success.
    // Safe to call again after close().
    virtual bool open() = 0;

    // Reads the next compressed packet into `packet`.
    // Returns true if a packet was read, false on end-of-stream or
    // unrecoverable error. On true, the caller owns `packet` and must
    // call av_packet_unref() once done with it.
    virtual bool readPacket(AVPacket* packet) = 0;

    // Releases all resources held by the source. Safe to call multiple
    // times and safe to call even if open() was never called or failed.
    virtual void close() = 0;

    // Returns true if the source is currently open.
    virtual bool isOpen() const = 0;
};

}  // namespace streamer
