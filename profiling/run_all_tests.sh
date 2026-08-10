#!/bin/bash
# run_all_tests.sh
# Runs all profiling scenarios and generates report
# Usage: ./run_all_tests.sh [duration_seconds]

set -e

DURATION="${1:-300}"  # Default 5 minutes per test
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Color output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}GENEA VIDEO STREAMER PROFILING SUITE${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Duration per test: ${DURATION} seconds"
echo "Time per scenario: ~$(($DURATION + 30)) seconds"
echo ""

# Cleanup any existing processes
pkill -f "/app/streamer\|./build/streamer" || true
sleep 2

# Prepare results directory
mkdir -p "$SCRIPT_DIR/results"
rm -f "$SCRIPT_DIR/results"/*

# Run all scenarios
SCENARIOS=("0_single_stream" "1_five_streams" "2_ten_streams" "3_fifty_streams")

for scenario in "${SCENARIOS[@]}"; do
    echo -e "${GREEN}Running: $scenario${NC}"
    bash "$SCRIPT_DIR/run_test.sh" "$scenario" "$DURATION"
    echo ""
    # Wait between tests
    sleep 5
done

# Generate report
echo -e "${GREEN}Generating report...${NC}"
bash "$SCRIPT_DIR/generate_report.sh"

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}PROFILING COMPLETE${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Results directory: $SCRIPT_DIR/results"
echo ""
