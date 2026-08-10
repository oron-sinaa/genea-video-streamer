#!/bin/bash
# run_profiling_in_docker.sh
# Helper script to run profiling tests inside Docker container
# Usage: ./run_profiling_in_docker.sh [duration_seconds]

set -e

DURATION="${1:-300}"  # Default 5 minutes per test
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Running profiling inside Docker container..."
echo "Duration per test: ${DURATION} seconds"
echo ""

# Make sure profiling/results directory exists
mkdir -p "$SCRIPT_DIR/profiling/results"

# Run profiling inside container
docker exec genea-video-streamer bash -c "
    cd /app && \
    ./profiling/run_all_tests.sh $DURATION
"

echo ""
echo "Profiling complete! Results are in: ./profiling/results/"
echo ""
echo "View results:"
echo "  cat ./profiling/results/REPORT.txt"
