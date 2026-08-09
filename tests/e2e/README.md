# End-to-End (E2E) Testing Suite

Comprehensive testing suite for the dockerized genea-video-streamer setup, validating real streaming scenarios with network failure simulation.

## Overview

The E2E test suite verifies the complete streaming pipeline:

1. **RTSP Stream Ingestion** - Mock RTSP server providing continuous video stream
2. **Packet Processing** - Real packet capture and HLS generation
3. **HLS Output** - Validation of playlist and segment file generation
4. **Health Metrics** - Monitoring of system health and process status
5. **Reconnect Behavior** - Network failure simulation and recovery validation

## Prerequisites

- Docker and docker-compose installed
- FFmpeg available for test video generation
- curl for HTTP validation
- Approximately 1GB disk space for test artifacts

## Quick Start

```bash
# Run all E2E tests
bash run_e2e_tests.sh

# View results
cat results/E2E_TEST_REPORT.txt
```

## Test Components

### 1. Test Video Generation (`generate_test_video.sh`)
Generates a 10-second test video (1280x720 H.264) for RTSP streaming.

**Output:** `test_video/test.mp4`

### 2. Docker Compose Setup (`docker-compose.test.yml`)

**Services:**

| Service | Purpose | Details |
|---------|---------|---------|
| **rtsp-server** | Mock RTSP stream | FFmpeg looping test video at rtsp://rtsp-server:554/stream1 |
| **streamer** | Main application | genea-video-streamer processing RTSP feed |
| **test-helper** | Validation tool | curl-based HTTP client for HLS validation |

**Network:** Custom bridge network (`e2e-test-network`) allows inter-container communication

### 3. HLS Validation (`validate_hls.sh`)

Verifies HLS output structure and accessibility:

- HTTP server responding
- live.m3u8 exists and contains HLS header
- Segments being generated (.ts files)
- Playlist metadata (TARGETDURATION, MEDIA-SEQUENCE)
- archive.m3u8 generation (if applicable)
- Segment file download accessibility
- Discontinuity markers (for reconnect scenarios)

**Output:** `results/hls_validation.txt`

### 4. Health Validation (`validate_health.sh`)

Checks system health and streamer process status:

- Container health (running/stopped)
- RTSP connection establishment
- Packet processing activity
- Error detection in logs
- Resource consumption (CPU, Memory)
- Segment file generation
- Playlist creation

**Output:** `results/health_validation.txt`

### 5. Reconnect Validation (`validate_reconnect.sh`)

Simulates network failure and validates recovery:

1. Baseline: Verify normal streaming
2. Simulate failure: Pause RTSP container
3. Detect reconnect attempts in logs
4. Restore connection: Unpause RTSP container
5. Verify recovery: Streamer resumes without crashing
6. Verify discontinuity markers added to playlist
7. Verify new segments being generated
8. Check backoff timing logs

**Scenario Timeline:**
- 0-5s: Normal streaming (capture baseline)
- 5-8s: Network failure (RTSP paused)
- 8-13s: Reconnect attempts (exponential backoff)
- 13s+: Connection restored
- 13-15s: Recovery and resumption

**Output:** `results/reconnect_validation.txt`

### 6. Test Orchestrator (`run_e2e_tests.sh`)

Master script that:
1. Validates Docker setup
2. Generates test artifacts
3. Starts docker-compose services
4. Waits for initialization
5. Runs all validation scripts
6. Collects logs and metrics
7. Generates comprehensive report

**Execution Flow:**
```
Start
  ↓
Generate test video
  ↓
Start Docker services
  ↓
Wait for initialization (10s)
  ↓
Validate HLS output
  ↓
Validate health metrics
  ↓
Test reconnect scenario (pause/resume RTSP)
  ↓
Collect final metrics
  ↓
Generate summary report
  ↓
Cleanup
  ↓
End
```

## Test Results

All results are saved to the `results/` directory:

| File | Contents |
|------|----------|
| `hls_validation.txt` | HLS playlist and segment validation |
| `health_validation.txt` | System health and container status |
| `reconnect_validation.txt` | Reconnect scenario test results |
| `E2E_TEST_REPORT.txt` | Combined summary report |

## Configuration

Test configuration is in `config/rtsp-ingest.yaml`:

```yaml
rtsp:
  url: "rtsp://rtsp-server:554/stream1"  # Mock RTSP server
  reconnect:
    initial_delay_ms: 500      # Fast reconnect for testing
    max_delay_ms: 5000         # Shorter max delay
    stale_timeout_s: 5         # Quicker stale detection

http:
  listen_port: 8000            # HTTP server for HLS

hls:
  segment_duration_s: 3        # Short segments for faster testing
  enable_discontinuity_markers: true
```

**Note:** Test values differ from production defaults for faster test execution.

## Troubleshooting

### Container won't start
```bash
docker-compose -f docker-compose.test.yml logs rtsp-server
docker-compose -f docker-compose.test.yml logs streamer
```

### HLS not generating
- Check streamer logs: `docker logs genea-streamer-test`
- Verify RTSP connection: `docker logs genea-rtsp-test-server`
- Check HTTP server: `curl http://localhost:8000/hls/`

### Reconnect test failing
- Ensure docker-compose is running: `docker ps`
- Check pause/resume worked: `docker inspect genea-rtsp-test-server | grep State`
- Review reconnect logs: `docker logs genea-streamer-test | grep -i reconnect`

### Permission issues
```bash
# Ensure scripts are executable
chmod +x run_e2e_tests.sh
chmod +x validate_*.sh
chmod +x generate_test_video.sh
```

## Test Isolation

Each E2E test run:
- Creates isolated docker-compose environment
- Uses separate network namespace
- Cleans up all containers and volumes on completion
- Can run multiple times without interference

## Expected Test Duration

- Test video generation: 5-10 seconds
- Service startup: 10 seconds
- HLS validation: 2-3 seconds
- Health validation: 2-3 seconds
- Reconnect scenario: 20 seconds
- Cleanup: 5 seconds

**Total: ~45-60 seconds**

## Integration with CI/CD

For GitHub Actions:

```yaml
- name: Run E2E Tests
  run: bash tests/e2e/run_e2e_tests.sh

- name: Upload E2E Results
  if: always()
  uses: actions/upload-artifact@v4
  with:
    name: e2e-test-results
    path: tests/e2e/results/
```

## Advanced Usage

### Run individual validation scripts

```bash
# Just HLS validation
bash validate_hls.sh results/hls.txt http://localhost:8000

# Just health validation
bash validate_health.sh results/health.txt genea-streamer-test

# Just reconnect scenario
bash validate_reconnect.sh results/reconnect.txt genea-rtsp-test-server genea-streamer-test
```

### Keep containers running for inspection

Edit `run_e2e_tests.sh` and comment out the `cleanup()` function calls to leave Docker containers running after tests complete.

### Manual scenario testing

```bash
# Start services manually
docker-compose -f docker-compose.test.yml up -d

# Run tests
bash validate_hls.sh
bash validate_health.sh
bash validate_reconnect.sh

# Inspect results
cat results/*.txt

# Manual cleanup
docker-compose -f docker-compose.test.yml down -v
```

## Success Criteria

✅ All tests pass if:
1. HLS playlist generation verified
2. Segment files accessible
3. No critical errors in logs
4. Reconnect detected and recovery successful
5. Discontinuity markers present after reconnect
6. Streaming resumed with new segments

---

**Next Steps:**
- Run E2E tests: `bash run_e2e_tests.sh`
- Review results: `cat results/E2E_TEST_REPORT.txt`
- Proceed to Phase 6 (Multi-stream Scalability) with confidence ✅
