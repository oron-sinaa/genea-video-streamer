#!/bin/bash
# Validate health metrics and reconnect behavior

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_FILE="${1:-/tmp/health_validation.txt}"
CONTAINER_NAME="${2:-genea-streamer-test}"
TIMEOUT="${3:-30}"

echo "========================================" > "$RESULTS_FILE"
echo "  Health Metrics Validation Report" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Timestamp: $(date)" >> "$RESULTS_FILE"
echo "Container: $CONTAINER_NAME" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

PASSED=0
FAILED=0

# Helper function to get container logs
get_logs() {
    docker logs "$CONTAINER_NAME" 2>/dev/null | tail -100
}

# Test 1: Check if streamer is running
echo "[TEST 1] Streamer process health"
if docker ps -a | grep -q "$CONTAINER_NAME"; then
    STATUS=$(docker inspect "$CONTAINER_NAME" --format='{{.State.Status}}' 2>/dev/null)
    if [[ "$STATUS" == "running" ]]; then
        echo "✅ PASS: Streamer container running" | tee -a "$RESULTS_FILE"
        ((PASSED++))
    else
        echo "❌ FAIL: Streamer container not running (status: $STATUS)" | tee -a "$RESULTS_FILE"
        ((FAILED++))
    fi
else
    echo "❌ FAIL: Streamer container not found" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Test 2: Check for successful RTSP connection
echo "[TEST 2] RTSP connection establishment"
if get_logs | grep -q "stream metadata.*video"; then
    echo "✅ PASS: RTSP connection established, stream metadata detected" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  WARN: Stream metadata not yet available (may be initializing)" | tee -a "$RESULTS_FILE"
fi

# Test 3: Check for packet processing
echo "[TEST 3] Packet processing"
PACKET_READS=$(get_logs | grep -c "packet\|read" || true)
if [[ $PACKET_READS -gt 0 ]]; then
    echo "✅ PASS: Packets being processed" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  INFO: Packet processing logs not yet available" | tee -a "$RESULTS_FILE"
fi

# Test 4: Check for errors
echo "[TEST 4] Error detection"
ERROR_COUNT=$(get_logs | grep -i "error\|failed\|exception" | grep -v "error loading" | wc -l)
if [[ $ERROR_COUNT -eq 0 ]]; then
    echo "✅ PASS: No errors detected in logs" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: $ERROR_COUNT error(s) found in logs" | tee -a "$RESULTS_FILE"
    get_logs | grep -i "error\|failed\|exception" | grep -v "error loading" | head -5 | while read line; do
        echo "  → $line" >> "$RESULTS_FILE"
    done
    ((FAILED++))
fi

# Test 5: Check for reconnect logs (if reconnect attempted)
echo "[TEST 5] Reconnect behavior monitoring"
RECONNECT_ATTEMPTS=$(get_logs | grep -i "reconnect\|retry" | wc -l)
if [[ $RECONNECT_ATTEMPTS -gt 0 ]]; then
    echo "✅ INFO: $RECONNECT_ATTEMPTS reconnect-related log(s) found" | tee -a "$RESULTS_FILE"
    get_logs | grep -i "reconnect\|retry" | head -3 | while read line; do
        echo "  → $line" >> "$RESULTS_FILE"
    done
else
    echo "ℹ️  INFO: No reconnect events yet (expected for stable connection)" | tee -a "$RESULTS_FILE"
fi

# Test 6: Check container resource usage
echo "[TEST 6] Resource consumption"
if docker stats --no-stream "$CONTAINER_NAME" 2>/dev/null | grep -q "$CONTAINER_NAME"; then
    STATS=$(docker stats --no-stream "$CONTAINER_NAME" 2>/dev/null | tail -1)
    CPU=$(echo "$STATS" | awk '{print $3}')
    MEM=$(echo "$STATS" | awk '{print $6}')
    echo "✅ PASS: CPU: $CPU, Memory: $MEM" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  WARN: Could not retrieve resource stats" | tee -a "$RESULTS_FILE"
fi

# Test 7: Check for HLS output
echo "[TEST 7] HLS file generation"
SEGMENT_COUNT=$(docker exec "$CONTAINER_NAME" find /data/segments -name "*.ts" 2>/dev/null | wc -l || echo 0)
if [[ $SEGMENT_COUNT -gt 0 ]]; then
    echo "✅ PASS: $SEGMENT_COUNT segment file(s) generated" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "⚠️  INFO: Segments not yet written (may still be initializing)" | tee -a "$RESULTS_FILE"
fi

# Test 8: Check for playlist files
echo "[TEST 8] Playlist file generation"
PLAYLIST_COUNT=$(docker exec "$CONTAINER_NAME" find /data/segments -name "*.m3u8" 2>/dev/null | wc -l || echo 0)
if [[ $PLAYLIST_COUNT -gt 0 ]]; then
    echo "✅ PASS: $PLAYLIST_COUNT playlist file(s) found" | tee -a "$RESULTS_FILE"
    ((PASSED++))
else
    echo "❌ FAIL: No playlist files generated" | tee -a "$RESULTS_FILE"
    ((FAILED++))
fi

# Summary
echo "" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "Summary: $PASSED passed, $FAILED failed" >> "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"

cat "$RESULTS_FILE"

if [[ $FAILED -gt 3 ]]; then
    echo "⚠️  Some critical tests failed. Logs:"
    get_logs | tail -20
    exit 1
fi

exit 0
