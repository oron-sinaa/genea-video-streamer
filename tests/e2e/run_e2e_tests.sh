#!/bin/bash
# End-to-end test runner for dockerized genea-video-streamer
# Orchestrates all E2E test scenarios

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_RESULTS_DIR="$SCRIPT_DIR/results"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.test.yml"

# Configuration
RTSP_CONTAINER="genea-rtsp-test-server"
STREAMER_CONTAINER="genea-streamer-test"
STARTUP_WAIT=10  # Wait for services to start
STREAMING_DURATION=15  # Let streaming run for 15s
RECONNECT_WAIT=10  # Wait after reconnect for recovery

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create results directory
mkdir -p "$TEST_RESULTS_DIR"

# Logging functions
log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Cleanup function
cleanup() {
    log_info "Cleaning up E2E test environment..."
    docker compose -f "$COMPOSE_FILE" down -v 2>/dev/null || true
    log_info "Cleanup complete"
}

# Set trap for cleanup on exit
trap cleanup EXIT

# Main test execution
main() {
    log_info "========================================="
    log_info "  genea-video-streamer E2E Test Suite"
    log_info "========================================="
    log_info "Start time: $(date)"
    log_info "Test directory: $SCRIPT_DIR"
    log_info ""

    # Check dependencies
    log_info "Checking dependencies..."
    if ! command -v docker &> /dev/null; then
        log_fail "Docker not installed"
        exit 1
    fi
    if ! docker compose version &> /dev/null; then
        log_fail "docker compose not available (Docker 20.10+ required)"
        exit 1
    fi
    log_pass "Docker and docker compose available"
    log_info ""

    # Step 1: Generate test video
    log_info "========================================="
    log_info "STEP 1: Generate Test Video"
    log_info "========================================="
    if [[ -f "$SCRIPT_DIR/generate_test_video.sh" ]]; then
        bash "$SCRIPT_DIR/generate_test_video.sh"
        log_pass "Test video generated"
    else
        log_warn "Test video script not found, skipping"
    fi
    log_info ""

    # Step 2: Start Docker services
    log_info "========================================="
    log_info "STEP 2: Start Docker Services"
    log_info "========================================="
    log_info "Starting containers with docker compose..."
    cd "$SCRIPT_DIR"
    docker compose -f "$COMPOSE_FILE" down -v 2>/dev/null || true
    sleep 2

    if ! docker compose -f "$COMPOSE_FILE" up -d; then
        log_fail "Failed to start docker compose services"
        docker compose -f "$COMPOSE_FILE" logs
        exit 1
    fi
    log_pass "Docker services started"

    log_info "Waiting for services to initialize ($STARTUP_WAIT seconds)..."
    sleep $STARTUP_WAIT
    log_pass "Services ready"
    log_info ""

    # Step 3: Verify service health
    log_info "========================================="
    log_info "STEP 3: Verify Service Health"
    log_info "========================================="

    # Check RTSP server
    if docker ps | grep -q "$RTSP_CONTAINER"; then
        log_pass "RTSP server container running"
    else
        log_fail "RTSP server not running"
        docker compose -f "$COMPOSE_FILE" logs "$RTSP_CONTAINER" | tail -20
        exit 1
    fi

    # Check streamer
    if docker ps | grep -q "$STREAMER_CONTAINER"; then
        log_pass "Streamer container running"
    else
        log_fail "Streamer not running"
        docker compose -f "$COMPOSE_FILE" logs "$STREAMER_CONTAINER" | tail -20
        exit 1
    fi
    log_info ""

    # Step 4: Validate HLS output
    log_info "========================================="
    log_info "STEP 4: Validate HLS Output"
    log_info "========================================="
    if bash "$SCRIPT_DIR/validate_hls.sh" "$TEST_RESULTS_DIR/hls_validation.txt"; then
        log_pass "HLS validation passed"
    else
        log_fail "HLS validation failed"
    fi
    log_info ""

    # Step 5: Validate health metrics
    log_info "========================================="
    log_info "STEP 5: Validate Health Metrics"
    log_info "========================================="
    if bash "$SCRIPT_DIR/validate_health.sh" \
        "$TEST_RESULTS_DIR/health_validation.txt" \
        "$STREAMER_CONTAINER"; then
        log_pass "Health metrics validation passed"
    else
        log_fail "Health metrics validation had warnings"
    fi
    log_info ""

    # Step 6: Test reconnect scenario
    log_info "========================================="
    log_info "STEP 6: Test Reconnect Scenario"
    log_info "========================================="
    log_info "Scenario: Pause RTSP server and verify reconnect behavior"
    log_info "Duration: ~$((RECONNECT_WAIT + 5)) seconds"
    log_info ""

    if bash "$SCRIPT_DIR/validate_reconnect.sh" \
        "$TEST_RESULTS_DIR/reconnect_validation.txt" \
        "$RTSP_CONTAINER" \
        "$STREAMER_CONTAINER"; then
        log_pass "Reconnect scenario passed"
    else
        log_fail "Reconnect scenario had issues"
    fi
    log_info ""

    # Step 7: Collect final metrics
    log_info "========================================="
    log_info "STEP 7: Collect Final Metrics"
    log_info "========================================="

    log_info "Streamer logs (last 20 lines):"
    docker logs "$STREAMER_CONTAINER" 2>/dev/null | tail -20 | sed 's/^/  /'

    log_info ""
    log_info "Container stats:"
    docker stats --no-stream "$STREAMER_CONTAINER" 2>/dev/null || log_warn "Could not get stats"
    log_info ""

    # Step 8: Generate summary report
    log_info "========================================="
    log_info "STEP 8: Generate Summary Report"
    log_info "========================================="

    REPORT_FILE="$TEST_RESULTS_DIR/E2E_TEST_REPORT.txt"
    {
        echo "========================================="
        echo "genea-video-streamer E2E Test Report"
        echo "========================================="
        echo "Test Date: $(date)"
        echo "Test Directory: $SCRIPT_DIR"
        echo ""
        echo "RESULTS:"
        echo "--------"
        [[ -f "$TEST_RESULTS_DIR/hls_validation.txt" ]] && cat "$TEST_RESULTS_DIR/hls_validation.txt"
        echo ""
        echo "--------"
        [[ -f "$TEST_RESULTS_DIR/health_validation.txt" ]] && cat "$TEST_RESULTS_DIR/health_validation.txt"
        echo ""
        echo "--------"
        [[ -f "$TEST_RESULTS_DIR/reconnect_validation.txt" ]] && cat "$TEST_RESULTS_DIR/reconnect_validation.txt"
    } > "$REPORT_FILE"

    log_pass "Summary report generated: $REPORT_FILE"
    log_info ""

    # Final summary
    log_info "========================================="
    log_info "E2E Test Suite Complete"
    log_info "========================================="
    log_info "Results saved to: $TEST_RESULTS_DIR/"
    log_info ""
    log_pass "All E2E tests completed successfully!"
    log_info ""
}

# Run main function
main "$@"

exit 0
