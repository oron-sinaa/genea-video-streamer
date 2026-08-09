#!/bin/bash
# Validate HLS output structure and content

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_FILE="${1:-/tmp/hls_validation.txt}"
STREAMER_HOST="${2:-http://localhost:8000}"
TIMEOUT="${3:-30}"

echo "========================================" > "$RESULTS_FILE"
echo "  HLS Validation Report" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Timestamp: $(date)" >> "$RESULTS_FILE"
echo "Target: $STREAMER_HOST" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

PASSED=0
FAILED=0

# Test 1: Check if HTTP server is responding
echo "[TEST 1] HTTP server connectivity"
if curl -s "$STREAMER_HOST/hls/" > /dev/null 2>&1; then
    echo "✅ PASS: HTTP server responding" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: HTTP server not responding" | tee -a "$RESULTS_FILE"
    ((FAILED++))
    exit 1
fi

# Test 2: Check live.m3u8 exists
echo "[TEST 2] Live playlist generation"
if curl -s "$STREAMER_HOST/hls/live.m3u8" | grep -q "EXT-X-VERSION"; then
    echo "✅ PASS: live.m3u8 exists and contains HLS header" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: live.m3u8 missing or invalid" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 3: Check for actual segments
echo "[TEST 3] Segment generation"
SEGMENTS=$(curl -s "$STREAMER_HOST/hls/live.m3u8" | grep -c "\.ts" || true)
if [[ $SEGMENTS -gt 0 ]]; then
    echo "✅ PASS: $SEGMENTS segments found in playlist" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: No segments found in live.m3u8" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 4: Check playlist duration
echo "[TEST 4] Playlist duration field"
if curl -s "$STREAMER_HOST/hls/live.m3u8" | grep -q "EXT-X-TARGETDURATION"; then
    DURATION=$(curl -s "$STREAMER_HOST/hls/live.m3u8" | grep "EXT-X-TARGETDURATION" | head -1)
    echo "✅ PASS: $DURATION" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: EXT-X-TARGETDURATION missing" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 5: Check media sequence
echo "[TEST 5] Media sequence tracking"
if curl -s "$STREAMER_HOST/hls/live.m3u8" | grep -q "EXT-X-MEDIA-SEQUENCE"; then
    SEQUENCE=$(curl -s "$STREAMER_HOST/hls/live.m3u8" | grep "EXT-X-MEDIA-SEQUENCE" | head -1)
    echo "✅ PASS: $SEQUENCE" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: EXT-X-MEDIA-SEQUENCE missing" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 6: Check archive.m3u8 (if exists)
echo "[TEST 6] Archive playlist"
if curl -s "$STREAMER_HOST/hls/archive.m3u8" | grep -q "EXT-X-VERSION"; then
    ARCHIVE_SEGMENTS=$(curl -s "$STREAMER_HOST/hls/archive.m3u8" | grep -c "\.ts" || true)
    echo "✅ PASS: archive.m3u8 exists with $ARCHIVE_SEGMENTS segments" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  WARN: archive.m3u8 not available (may not exist yet)" | tee -a "$RESULTS_FILE"
fi

# Test 7: Check segment file accessibility
echo "[TEST 7] Segment file download"
FIRST_SEGMENT=$(curl -s "$STREAMER_HOST/hls/live.m3u8" | grep "\.ts" | head -1)
if [[ -n "$FIRST_SEGMENT" ]]; then
    SEGMENT_URL="$STREAMER_HOST/hls/$FIRST_SEGMENT"
    if curl -s -o /dev/null -w "%{http_code}" "$SEGMENT_URL" | grep -q "200\|206"; then
        echo "✅ PASS: Segment accessible ($FIRST_SEGMENT)" | tee -a "$RESULTS_FILE"
        ((PASSED++))
    else
        echo "❌ FAIL: Cannot download segment" | tee -a "$RESULTS_FILE"
        ((FAILED++))
    fi
else
    echo "⚠️  WARN: No segments to test yet" | tee -a "$RESULTS_FILE"
fi

# Test 8: Check for discontinuity markers (after reconnect)
echo "[TEST 8] Discontinuity markers"
DISCONTINUITY=$(curl -s "$STREAMER_HOST/hls/live.m3u8" | grep -c "DISCONTINUITY" || true)
if [[ $DISCONTINUITY -gt 0 ]]; then
    echo "✅ PASS: $DISCONTINUITY discontinuity marker(s) found" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  INFO: No discontinuity markers (expected if no reconnects)" | tee -a "$RESULTS_FILE"
fi

# Summary
echo "" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Summary: $PASSED passed, $FAILED failed" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"

cat "$RESULTS_FILE"

if [[ $FAILED -gt 0 ]]; then
    exit 1
fi

exit 0
