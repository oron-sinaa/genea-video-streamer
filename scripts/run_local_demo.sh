#!/bin/bash
#
# run_local_demo.sh - Complete end-to-end demo: generate test video, stream, and open player.
#
# Usage:
#   ./scripts/run_local_demo.sh
#
# This script:
#   1. Generates a test video (if not present)
#   2. Starts an RTSP server to stream the test video
#   3. Starts the genea-video-streamer binary
#   4. Starts the HTTP server for the web player
#   5. Opens the player in the default browser
#   6. On Ctrl+C, cleans up all processes
#
# Requirements:
#   - ffmpeg (for generating test video and RTSP server)
#   - Python 3 (for HTTP server)
#   - xdg-open or similar (for opening browser; macOS uses 'open')

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Configuration
TEST_VIDEO="$REPO_ROOT/test_video.mp4"
TEST_VIDEO_DURATION=60  # seconds
RTSP_PORT=8554
HTTP_PORT=8080

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

cleanup() {
    echo ""
    echo -e "${YELLOW}Shutting down all processes...${NC}"
    
    # Kill background processes
    jobs -p | xargs -r kill 2>/dev/null || true
    
    # Give processes time to exit gracefully
    sleep 1
    
    # Force kill if necessary
    pkill -f "ffmpeg.*rtsp" 2>/dev/null || true
    pkill -f "python3.*http.server" 2>/dev/null || true
    pkill -f "streamer" 2>/dev/null || true
    
    echo -e "${GREEN}All processes stopped.${NC}"
    exit 0
}

trap cleanup SIGINT SIGTERM

echo -e "${BLUE}=========================================="
echo "Genea Video Streamer - Local Demo"
echo "==========================================${NC}"
echo ""

# Check dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"
for cmd in ffmpeg python3; do
    if ! command -v "$cmd" &> /dev/null; then
        echo -e "${RED}Error: $cmd is required but not installed.${NC}"
        exit 1
    fi
done
echo -e "${GREEN}✓ Dependencies OK${NC}"
echo ""

# Generate test video if it doesn't exist
if [ ! -f "$TEST_VIDEO" ]; then
    echo -e "${YELLOW}Generating test video ($TEST_VIDEO_DURATION seconds)...${NC}"
    ffmpeg -f lavfi -i testsrc=size=1280x720:duration=$TEST_VIDEO_DURATION:rate=30 \
           -f lavfi -i sine=frequency=1000:duration=$TEST_VIDEO_DURATION \
           -pix_fmt yuv420p -c:v libx264 -preset veryfast -c:a aac \
           "$TEST_VIDEO" -y > /dev/null 2>&1
    echo -e "${GREEN}✓ Test video created: $TEST_VIDEO${NC}"
else
    echo -e "${GREEN}✓ Test video already exists: $TEST_VIDEO${NC}"
fi
echo ""

# Build streamer if not built yet
if [ ! -f "$REPO_ROOT/build/streamer" ]; then
    echo -e "${YELLOW}Building streamer...${NC}"
    cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build" -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    cmake --build "$REPO_ROOT/build" -j"$(nproc)" > /dev/null 2>&1
    echo -e "${GREEN}✓ Streamer built: $REPO_ROOT/build/streamer${NC}"
else
    echo -e "${GREEN}✓ Streamer already built${NC}"
fi
echo ""

# Create segments directory
mkdir -p "$REPO_ROOT/segments"

# Start RTSP server (ffmpeg streaming the test video)
echo -e "${YELLOW}Starting RTSP server on rtsp://localhost:$RTSP_PORT/stream${NC}"
ffmpeg -re -i "$TEST_VIDEO" -c copy -f rtsp "rtsp://localhost:$RTSP_PORT/stream" > /dev/null 2>&1 &
RTSP_PID=$!
sleep 2  # Give RTSP server time to start
echo -e "${GREEN}✓ RTSP server started (PID: $RTSP_PID)${NC}"
echo ""

# Update config to point to local RTSP server
echo -e "${YELLOW}Configuring streamer for local RTSP...${NC}"
cat > "$REPO_ROOT/config/local-demo.yaml" <<EOF
rtsp:
  url: "rtsp://127.0.0.1:$RTSP_PORT/stream"
  transport: "tcp"
  timeout_us: 5000000

hls:
  output_dir: "segments"
  segment_duration_s: 3
  archive_retention_hours: 2
  cleanup_interval_s: 300
EOF
echo -e "${GREEN}✓ Config created: config/local-demo.yaml${NC}"
echo ""

# Start streamer
echo -e "${YELLOW}Starting streamer...${NC}"
"$REPO_ROOT/build/streamer" "$REPO_ROOT/config/local-demo.yaml" &
STREAMER_PID=$!
sleep 2  # Give streamer time to start and create first segments
echo -e "${GREEN}✓ Streamer started (PID: $STREAMER_PID)${NC}"
echo ""

# Start HTTP server
echo -e "${YELLOW}Starting HTTP server on http://localhost:$HTTP_PORT${NC}"
cd "$REPO_ROOT"
python3 -m http.server $HTTP_PORT > /dev/null 2>&1 &
HTTP_PID=$!
sleep 1
echo -e "${GREEN}✓ HTTP server started (PID: $HTTP_PID)${NC}"
echo ""

# Determine browser open command
if command -v xdg-open &> /dev/null; then
    OPEN_CMD="xdg-open"
elif command -v open &> /dev/null; then
    OPEN_CMD="open"
else
    OPEN_CMD=""
fi

# Open browser if possible
PLAYER_URL="http://localhost:$HTTP_PORT/web/player.html"
if [ -n "$OPEN_CMD" ]; then
    echo -e "${YELLOW}Opening player in browser...${NC}"
    $OPEN_CMD "$PLAYER_URL" 2>/dev/null || true
    echo -e "${GREEN}✓ Player opened${NC}"
else
    echo -e "${YELLOW}Open the player manually:${NC}"
    echo -e "${BLUE}$PLAYER_URL${NC}"
fi
echo ""

echo -e "${GREEN}=========================================="
echo "Demo is running!"
echo "==========================================${NC}"
echo ""
echo "Active processes:"
echo "  - RTSP server (PID: $RTSP_PID)"
echo "  - Streamer (PID: $STREAMER_PID)"
echo "  - HTTP server (PID: $HTTP_PID)"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all services.${NC}"
echo ""

# Keep script running
wait
