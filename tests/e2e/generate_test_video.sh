#!/bin/bash
# Generate a simple test video for E2E testing
# Creates a 10-second silent video loop with color frames

set -e

TEST_VIDEO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/test_video"
mkdir -p "$TEST_VIDEO_DIR"

TEST_VIDEO="$TEST_VIDEO_DIR/test.mp4"

echo "Generating test video: $TEST_VIDEO"

# Create 10-second video with solid color frames
# Using drawtext to show timestamp
ffmpeg -f lavfi -i color=c=blue:s=1280x720:d=10 \
       -f lavfi -i sine=f=1000:d=10 \
       -pix_fmt yuv420p \
       -c:v libx264 -preset fast -crf 20 \
       -c:a aac -b:a 128k \
       -y "$TEST_VIDEO" 2>&1 | grep -E "frame|Duration|bitrate" || true

if [[ -f "$TEST_VIDEO" ]]; then
    echo "✅ Test video created: $(ls -lh $TEST_VIDEO | awk '{print $5, $9}')"
else
    echo "❌ Failed to create test video"
    exit 1
fi
