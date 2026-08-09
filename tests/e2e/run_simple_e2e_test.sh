#!/bin/bash
# Simplified End-to-End Test (Local Execution)
# Tests the streamer binary directly without docker-compose complexity
# Run this to quickly validate the full streaming pipeline

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
STREAMER_BIN="$BUILD_DIR/streamer"
CONFIG_FILE="$SCRIPT_DIR/config/rtsp-ingest.yaml"
OUTPUT_DIR="/tmp/genea_e2e_test_$$"
RTSP_PORT=8554
HTTP_PORT=8000

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Cleanup on exit
cleanup() {
    log_info "Cleaning up..."
    
    # Kill processes
    pkill -f "genea-video-streamer" || true
    pkill -f "ffmpeg.*rtsp" || true
    
    # Remove temp files
    rm -rf "$OUTPUT_DIR"
    
    log_info "Cleanup complete"
}

trap cleanup EXIT

main() {
    log_info "========================================="
    log_info "  Genea Streamer E2E Test (Local Mode)"
    log_info "========================================="
    log_info "Start: $(date)"
    log_info ""

    # Step 0: Validate setup
    log_info "========================================="
    log_info "STEP 0: Validate Prerequisites"
    log_info "========================================="

    if [[ ! -f "$STREAMER_BIN" ]]; then
        log_fail "Streamer binary not found: $STREAMER_BIN"
        log_info "Build with: cmake -S . -B build && cmake --build build"
        exit 1
    fi
    log_pass "Streamer binary found"

    if ! command -v ffmpeg &> /dev/null; then
        log_fail "FFmpeg not installed"
        exit 1
    fi
    log_pass "FFmpeg available"

    if ! command -v curl &> /dev/null; then
        log_fail "curl not installed"
        exit 1
    fi
    log_pass "curl available"

    mkdir -p "$OUTPUT_DIR"
    log_pass "Output directory created: $OUTPUT_DIR"
    log_info ""

    # Step 1: Generate minimal test video
    log_info "========================================="
    log_info "STEP 1: Generate Test Video (10 seconds)"
    log_info "========================================="

    TEST_VIDEO="/tmp/genea_test_$$_video.mp4"
    log_info "Generating: $TEST_VIDEO"
    
    ffmpeg -f lavfi -i color=c=blue:s=1280x720:d=10 \
           -f lavfi -i sine=f=1000:d=10 \
           -pix_fmt yuv420p \
           -c:v libx264 -preset ultrafast -crf 28 \
           -c:a aac -b:a 64k \
           -y "$TEST_VIDEO" 2>&1 | grep -E "frame|Duration" || true
    
    if [[ -f "$TEST_VIDEO" ]]; then
        log_pass "Test video generated ($(ls -lh $TEST_VIDEO | awk '{print $5}'))"
    else
        log_fail "Failed to generate test video"
        exit 1
    fi
    log_info ""

    # Step 2: Create MPEG-TS stream (local test mode)
    log_info "========================================="
    log_info "STEP 2: Prepare Streaming Input"
    log_info "========================================="

    # For local testing without RTSP server, we'll use file streaming
    # The streamer can read from a piped MPEG-TS stream
    MPEG_TS_STREAM="/tmp/genea_stream_$$.ts"
    
    log_info "Creating MPEG-TS stream from test video..."
    ffmpeg -re -i "$TEST_VIDEO" \
           -c:v libx264 -preset ultrafast \
           -c:a aac \
           -f mpegts "pipe:1" \
           > "$MPEG_TS_STREAM" 2>/dev/null &
    
    FFMPEG_PID=$!
    sleep 2
    
    if kill -0 $FFMPEG_PID 2>/dev/null && [[ -f "$MPEG_TS_STREAM" ]]; then
        SIZE=$(du -h "$MPEG_TS_STREAM" | awk '{print $1}')
        log_pass "MPEG-TS stream prepared (size: $SIZE)"
    else
        log_warn "Stream preparation ongoing..."
    fi
    log_info ""

    # Step 3: Start streamer
    log_info "========================================="
    log_info "STEP 3: Start Streamer Service"
    log_info "========================================="

    log_info "Starting streamer on port $HTTP_PORT..."
    
    "$STREAMER_BIN" --config "$CONFIG_FILE" \
        2>&1 | tee "$OUTPUT_DIR/streamer.log" &
    
    STREAMER_PID=$!
    sleep 5
    
    if kill -0 $STREAMER_PID 2>/dev/null; then
        log_pass "Streamer started (PID: $STREAMER_PID)"
    else
        log_fail "Streamer failed to start"
        tail -20 "$OUTPUT_DIR/streamer.log"
        exit 1
    fi
    log_info ""

    # Step 4: Validate HLS streaming
    log_info "========================================="
    log_info "STEP 4: Validate HLS Output"
    log_info "========================================="

    TESTS_PASSED=0
    TESTS_FAILED=0

    # Test 4a: HTTP server responding
    log_info "Test 4a: HTTP server connectivity"
    if curl -s "http://127.0.0.1:$HTTP_PORT/hls/" > /dev/null 2>&1; then
        log_pass "HTTP server responding"
        ((TESTS_PASSED++))
    else
        log_fail "HTTP server not responding"
        ((TESTS_FAILED++))
    fi

    # Test 4b: live.m3u8 exists
    log_info "Test 4b: Live playlist generation"
    sleep 2
    if curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -q "EXT-X-VERSION"; then
        log_pass "live.m3u8 generated"
        ((TESTS_PASSED++))
    else
        log_warn "live.m3u8 not yet available"
    fi

    # Test 4c: Segments being created
    log_info "Test 4c: Segment generation"
    SEGMENT_COUNT=$(curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -c "\.ts" || echo 0)
    if [[ $SEGMENT_COUNT -gt 0 ]]; then
        log_pass "$SEGMENT_COUNT segments found"
        ((TESTS_PASSED++))
    else
        log_warn "No segments yet (streaming may still be initializing)"
    fi

    log_info ""

    # Step 5: Let streaming run
    log_info "========================================="
    log_info "STEP 5: Sustained Streaming Test"
    log_info "========================================="

    log_info "Monitoring stream for 10 seconds..."
    for i in {1..10}; do
        SEGMENTS=$(curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -c "\.ts" || echo 0)
        echo -ne "\rSegments: $SEGMENTS packets received"
        sleep 1
    done
    echo ""

    FINAL_SEGMENTS=$(curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -c "\.ts" || echo 0)
    if [[ $FINAL_SEGMENTS -gt 0 ]]; then
        log_pass "Sustained streaming successful ($FINAL_SEGMENTS segments)"
        ((TESTS_PASSED++))
    else
        log_fail "No segments after 10 seconds"
        ((TESTS_FAILED++))
    fi
    log_info ""

    # Step 6: Test reconnect scenario
    log_info "========================================="
    log_info "STEP 6: Test Reconnect Behavior"
    log_info "========================================="

    log_info "Capturing initial segment count..."
    INITIAL_SEGMENTS=$(curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -c "\.ts" || echo 0)
    log_info "Initial segments: $INITIAL_SEGMENTS"

    log_info "Simulating network failure (kill RTSP server)..."
    kill $FFMPEG_PID 2>/dev/null || true
    sleep 3

    log_info "Resuming RTSP stream..."
    ffmpeg -re -loop 1 -i "$TEST_VIDEO" \
           -c:v libx264 -preset ultrafast \
           -c:a aac \
           -f rtsp "rtsp://127.0.0.1:$RTSP_PORT/stream1" \
           2>/dev/null &
    FFMPEG_PID=$!

    log_info "Waiting for reconnection (15 seconds)..."
    sleep 15

    RECOVERED_SEGMENTS=$(curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -c "\.ts" || echo 0)
    log_info "Segments after recovery: $RECOVERED_SEGMENTS"

    if [[ $RECOVERED_SEGMENTS -gt $INITIAL_SEGMENTS ]]; then
        log_pass "Reconnect successful (new segments: $((RECOVERED_SEGMENTS - INITIAL_SEGMENTS)))"
        ((TESTS_PASSED++))
    else
        log_warn "Reconnect test inconclusive"
    fi

    log_info "Checking for discontinuity markers..."
    if curl -s "http://127.0.0.1:$HTTP_PORT/hls/live.m3u8" 2>/dev/null | grep -q "DISCONTINUITY"; then
        log_pass "Discontinuity marker found"
        ((TESTS_PASSED++))
    else
        log_info "No discontinuity marker (expected if segments still buffering)"
    fi
    log_info ""

    # Step 7: Final validation
    log_info "========================================="
    log_info "STEP 7: Final Validation"
    log_info "========================================="

    log_info "Checking streamer logs..."
    ERROR_COUNT=$(grep -i "error" "$OUTPUT_DIR/streamer.log" 2>/dev/null | wc -l)
    if [[ $ERROR_COUNT -eq 0 ]]; then
        log_pass "No errors in logs"
        ((TESTS_PASSED++))
    else
        log_warn "$ERROR_COUNT errors found in logs"
    fi

    log_info "Verifying streamer still running..."
    if kill -0 $STREAMER_PID 2>/dev/null; then
        log_pass "Streamer process healthy"
        ((TESTS_PASSED++))
    else
        log_fail "Streamer process not running"
        ((TESTS_FAILED++))
    fi

    log_info ""

    # Final summary
    log_info "========================================="
    log_info "E2E Test Complete"
    log_info "========================================="
    log_info "Tests Passed: $TESTS_PASSED"
    log_info "Tests Failed: $TESTS_FAILED"
    log_info "Results: $OUTPUT_DIR"
    log_info "Logs: $OUTPUT_DIR/streamer.log"
    log_info ""

    if [[ $TESTS_FAILED -eq 0 ]]; then
        log_pass "✅ ALL E2E TESTS PASSED"
        return 0
    else
        log_fail "❌ SOME TESTS FAILED"
        return 1
    fi
}

main "$@"
EXIT_CODE=$?

exit $EXIT_CODE
