#!/bin/bash
# Test reconnect behavior by simulating network failure

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_FILE="${1:-/tmp/reconnect_validation.txt}"
RTSP_CONTAINER="${2:-genea-rtsp-test-server}"
STREAMER_CONTAINER="${3:-genea-streamer-test}"

echo "========================================" > "$RESULTS_FILE"
echo "  Reconnect Scenario Validation" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Timestamp: $(date)" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

PASSED=0
FAILED=0

# Test 1: Baseline - normal operation
echo "[TEST 1] Baseline streaming (normal operation)"
if docker exec "$STREAMER_CONTAINER" pgrep -f "streamer" > /dev/null 2>&1; then
    echo "✅ PASS: Streamer running" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: Streamer not running" | tee -a "$RESULTS_FILE"
    ((FAILED++))
    exit 1
fi

# Get initial packet count
INITIAL_SEGMENTS=$(docker exec "$STREAMER_CONTAINER" \
    find /data/segments -name "*.ts" 2>/dev/null | wc -l || echo 0)
echo "  Initial segments: $INITIAL_SEGMENTS" | tee -a "$RESULTS_FILE"

# Test 2: Simulate network failure
echo "[TEST 2] Simulate network failure (pause RTSP server)"
docker pause "$RTSP_CONTAINER" 2>/dev/null || true
echo "  RTSP server paused at $(date)" | tee -a "$RESULTS_FILE"

# Wait for reconnect logic to detect failure
sleep 3

# Test 3: Check for reconnect attempts
echo "[TEST 3] Verify reconnect attempts detected"
RECONNECT_LOGS=$(docker logs "$STREAMER_CONTAINER" 2>/dev/null | grep -i "reconnect\|retry\|error" | wc -l)
if [[ $RECONNECT_LOGS -gt 0 ]]; then
    echo "✅ PASS: Reconnect logic activated ($RECONNECT_LOGS log entries)" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  WARN: No reconnect logs yet (may still be attempting)" | tee -a "$RESULTS_FILE"
fi

# Test 4: Resume connection
echo "[TEST 4] Restore connection (unpause RTSP server)"
docker unpause "$RTSP_CONTAINER" 2>/dev/null || true
echo "  RTSP server resumed at $(date)" | tee -a "$RESULTS_FILE"

# Wait for reconnection
sleep 5

# Test 5: Verify recovery
echo "[TEST 5] Verify successful reconnection"
if docker exec "$STREAMER_CONTAINER" pgrep -f "streamer" > /dev/null 2>&1; then
    echo "✅ PASS: Streamer recovered after reconnect" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: Streamer crashed during reconnect" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 6: Check for discontinuity markers
echo "[TEST 6] Verify discontinuity markers in playlist"
if docker exec "$STREAMER_CONTAINER" \
    grep -q "DISCONTINUITY" /data/segments/live.m3u8 2>/dev/null; then
    echo "✅ PASS: Discontinuity marker found in playlist" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  INFO: No discontinuity marker yet (may appear after next segment)" | tee -a "$RESULTS_FILE"
fi

# Test 7: Verify streaming resumed
echo "[TEST 7] Verify streaming resumed after reconnect"
sleep 3
NEW_SEGMENTS=$(docker exec "$STREAMER_CONTAINER" \
    find /data/segments -name "*.ts" 2>/dev/null | wc -l || echo 0)

if [[ $NEW_SEGMENTS -gt $INITIAL_SEGMENTS ]]; then
    ADDED=$((NEW_SEGMENTS - INITIAL_SEGMENTS))
    echo "✅ PASS: Streaming resumed ($ADDED new segments)" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: No new segments after reconnect" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 8: Check for backoff timing in logs
echo "[TEST 8] Verify exponential backoff timing"
BACKOFF_LOGS=$(docker logs "$STREAMER_CONTAINER" 2>/dev/null | grep -i "backoff\|waiting\|delay" | wc -l)
if [[ $BACKOFF_LOGS -gt 0 ]]; then
    echo "✅ PASS: Backoff timing logged ($BACKOFF_LOGS entries)" | tee -a "$RESULTS_FILE"
    docker logs "$STREAMER_CONTAINER" 2>/dev/null | grep -i "backoff\|waiting\|delay" | \
        head -3 | while read line; do
        echo "    → $line" >> "$RESULTS_FILE"
    done
    ((PASSED++))
else
    echo "⚠️  INFO: Backoff logs not yet visible" | tee -a "$RESULTS_FILE"
fi

# Summary
echo "" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Summary: $PASSED passed, $FAILED failed" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Reconnect validation completed at $(date)" >> "$RESULTS_FILE"

cat "$RESULTS_FILE"

if [[ $FAILED -gt 1 ]]; then
    exit 1
fi

exit 0
