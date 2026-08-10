#!/bin/bash
# run_test.sh
# Runs a single test scenario
# Usage: run_test.sh <scenario> [duration]

set -e

SCENARIO="${1:-baseline}"
DURATION="${2:-300}"  # Default 5 minutes (300 seconds)
PORT=8080

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
BINARY="$PROJECT_DIR/streamer"
CONFIG="$SCRIPT_DIR/scenarios/${SCENARIO}.yaml"

# Verify files exist
if [ ! -f "$BINARY" ]; then
    echo "ERROR: streamer binary not found at $BINARY"
    echo "Please run: cd $PROJECT_DIR && mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "ERROR: config not found at $CONFIG"
    exit 1
fi

# Create results directory
mkdir -p "$RESULTS_DIR"

# Cleanup function
cleanup() {
    echo "Stopping test..."
    if [ -n "$STREAMER_PID" ] && kill -0 "$STREAMER_PID" 2>/dev/null; then
        kill $STREAMER_PID 2>/dev/null || true
        sleep 1
        # Force kill if still running
        kill -9 $STREAMER_PID 2>/dev/null || true
    fi
    if [ -n "$METRICS_PID" ] && kill -0 "$METRICS_PID" 2>/dev/null; then
        kill $METRICS_PID 2>/dev/null || true
    fi
}

trap cleanup EXIT

# Start streamer
echo "Starting test: $SCENARIO (duration: ${DURATION}s)"
echo "Config: $CONFIG"

"$BINARY" "$CONFIG" > "$RESULTS_DIR/${SCENARIO}.log" 2>&1 &
STREAMER_PID=$!

# Wait for HTTP server to start
echo "Waiting for server to start..."
for i in {1..30}; do
    if curl -s "http://localhost:$PORT/api/health" > /dev/null 2>&1; then
        echo "Server ready"
        break
    fi
    if ! kill -0 $STREAMER_PID 2>/dev/null; then
        echo "ERROR: streamer crashed during startup"
        cat "$RESULTS_DIR/${SCENARIO}.log"
        exit 1
    fi
    sleep 1
done

# Start metrics collection
echo "Collecting metrics for $DURATION seconds..."
bash "$SCRIPT_DIR/collect_metrics.sh" $PORT $DURATION "$RESULTS_DIR/${SCENARIO}_metrics.csv" &
METRICS_PID=$!

# Wait for test to complete
sleep $((DURATION + 5))

# Kill metrics collection if still running
if kill -0 $METRICS_PID 2>/dev/null; then
    kill $METRICS_PID
    wait $METRICS_PID 2>/dev/null || true
fi

echo "Test complete: $SCENARIO"
echo "Results: $RESULTS_DIR/${SCENARIO}_metrics.csv"
echo "Log: $RESULTS_DIR/${SCENARIO}.log"
