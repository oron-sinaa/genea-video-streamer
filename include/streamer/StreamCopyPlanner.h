#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <string>

namespace streamer {

// Result of stream copy compatibility check.
struct StreamCopyResult {
    bool canCopy = false;
    std::string reason;  // If canCopy=false, explains why
};

// Validates whether a source stream can be directly copied (remuxed without
// re-encoding) into an MPEG-TS container.
//
// Motivation: Codec copy requires:
//  1. Container format (MPEG-TS) supports the codec
//  2. Codec extradata is present and valid
//  3. No unsupported codec profiles/levels
class StreamCopyPlanner {
public:
    ~StreamCopyPlanner() = default;

    // Checks if `sourceStream` can be copied into an MPEG-TS container.
    // Returns a result with canCopy=true if viable, false with reason if not.
    //
    // Checks:
    //  - Codec is in MPEG-TS whitelist (H.264, H.265, MPEG-2, AAC, MP3, etc.)
    //  - Codec parameters are valid (width > 0 for video, etc.)
    //  - Extradata hints at a complete codec profile
    static StreamCopyResult canCopyToMpegTs(const AVStream* sourceStream);

private:
    // Whitelist of codecs safe for MPEG-TS container.
    // Returns true if codec is safe to copy into MPEG-TS, false otherwise.
    static bool isCodecMpegTsCompatible(AVCodecID codecId);

    // Validates codec-specific requirements (extradata, profile, etc.).
    static bool isCodecParamsValid(const AVCodecParameters* params);
};

}  // namespace streamer
