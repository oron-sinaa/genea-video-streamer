# genea-video-streamer
Implementation of streaming and inference solution for the Genea interview assessment.

## Goal

Implement an open-source live video streaming solution using C++ and LibAV that:

1. Captures from IP camera via RTSP only.
2. Streams to a destination endpoint.
3. Supports browser-based live view and playback.
4. Handles network outages and common failure conditions.

## Technical Direction

- Language: C++
- Media stack: LibAV (`libavformat`, `libavcodec`, `libavutil`, `libswscale`)
- Delivery protocol: HLS (open standard) through packet remuxing
- Web playback: HLS.js-based player
- Video policy: codec copy only (`-c copy` equivalent). No transcoding/encoding.

## Scope Limitation

1. Ingest source is RTSP only.
2. Output is produced by remuxing compressed packets, not decoding/re-encoding frames.
3. Browser compatibility depends on source camera codec and GOP settings.
4. Recommended camera profile for MVP: H.264 stream with regular keyframes (for stable HLS playback).

## Working Style

1. Keep implementation simple and human-readable.
2. Do not perform premature optimization.
3. Build strictly step by step, validating each stage before moving forward.

## Implementation Chronology (Priority Order)

1. Foundation: CMake project, config model, logging, error handling conventions.
2. Capture: RTSP input and stream probing.
3. Pipeline core: demux packet ingest -> timestamp normalization -> packet remux.
4. Streaming output: HLS segment + playlist generation for live and playback (codec copy only).
5. Web player: browser UI for live playback and archive playback.
6. Reliability: reconnect/backoff, health metrics, outage behavior.
7. Quality: unit/integration tests and CI checks.
8. Scalability: multi-stream pipeline manager and deployment guidance.
9. Optional: AI inference, object search, performance profiling.

## Design Document

Detailed technical design, architecture, module breakdown, and implementation plan are in [docs/design.md](docs/design.md).

### Quick Roadmap (1-Week Deadline)

| Phase | Name | Status | Eval Criteria |
|-------|------|--------|---------------|
| 0 | Foundation | ✅ Done | Code Quality |
| 1 | Capture & Probe | ✅ Done | Video Capture |
| 2 | Remux & Segments | 🔄 Next | Streaming Protocol, Functionality |
| 3 | Web Player | ⏳ Planned | Web Player, Functionality |
| 4 | Reliability | ⏳ Planned | Network Outage Handling, Reliability |
| 5 | Testing & CI | ⏳ Planned | Testing, Code Quality |
| 6 | Scalability | 📅 Optional | Scalability |
| 7 | AI/Optional | 📅 Optional | (Optional Task) |

See [docs/design.md § 10](docs/design.md#10-implementation-plan-aligned-with-evaluation-criteria) for full implementation plan with time estimates and evaluation criteria alignment.

## Current Status

**Phases 0–4 Complete:**
- ✅ CMake build system with LibAV + yaml-cpp
- ✅ YAML configuration loader with validation
- ✅ Logging framework (INFO/WARN/ERROR macros)
- ✅ RTSP source ingest (LibAV wrapper)
- ✅ Stream metadata probe (codec, resolution, fps, time base)
- ✅ Packet read loop with compressed packet handling
- ✅ Packet clock for timestamp normalization (monotonic enforcement, jitter handling)
- ✅ HLS remux + segment generation with live/archive playlists
- ✅ HTTP server for HLS streaming and playback
- ✅ Web player UI for live and archive viewing
- ✅ Automatic reconnect with exponential backoff (1s → 30s max)
- ✅ Stale source detection (10s timeout)
- ✅ HLS discontinuity markers for smooth playback through reconnects
- ✅ Pipeline health metrics (packets, reconnects, throughput)

**Phase 5 (Testing & CI/CD) Complete:**
- ✅ Unit tests: ReconnectPolicy (5 tests), PipelineHealth (6 tests)
- ✅ Integration tests: Reconnect scenarios, backoff progression, stale detection (4 scenarios)
- ✅ GitHub Actions CI/CD pipeline (build, test, docker build)
- ✅ Production Docker setup (multi-stage build, minimal runtime image)
- ✅ docker-compose for easy local deployment
- ✅ Health checks and resource limits configured

**Next (Phases 6–7):**
- Scalability: multi-stream pipeline manager
- Optional: AI inference, object search, performance profiling

**Timeline:** Phases 0–5 complete (~40 hours); ready for production deployment.

## Quick Start

### Local Development (Standalone Binary)

```bash
# Build
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run (edit config/rtsp-ingest.yaml first)
./build/streamer --config config/rtsp-ingest.yaml

# Tests
./build/test_reconnect_policy
./build/test_pipeline_health
./tests/integration/run_integration_tests.sh
```

### Production (Docker)

#### Build Image

```bash
docker build -t genea-streamer:latest .
```

#### Run with docker-compose (Recommended)

```bash
# Edit docker-compose.yml for your RTSP URL and output directory
docker-compose up -d

# Check logs
docker-compose logs -f streamer

# Stop
docker-compose down
```

#### Run Standalone Container

```bash
docker run -d \
  --name genea-streamer \
  -v $(pwd)/config:/etc/streamer:ro \
  -v $(pwd)/segments:/data/segments \
  -e RTSP_URL="rtsp://camera-ip:554/stream" \
  genea-streamer:latest
```

#### Verify Health

```bash
docker ps
docker logs genea-streamer
```

### Configuration

Edit `config/rtsp-ingest.yaml`:

```yaml
rtsp:
  url: "rtsp://camera-ip:554/stream"
  transport: "tcp"
  timeout_us: 5000000
  reconnect:
    enabled: true
    initial_delay_ms: 1000
    max_delay_ms: 30000
    jitter_percent: 15
    stale_timeout_s: 10

http:
  listen_port: 8000
  hls_path: "/hls"

hls:
  output_dir: "segments"
  segment_duration_s: 3
  archive_retention_hours: 2
  enable_discontinuity_markers: true
```

### CI/CD

GitHub Actions pipeline runs automatically on push:
- ✅ Build and compile
- ✅ Unit tests (ReconnectPolicy, PipelineHealth)
- ✅ Integration tests (reconnect scenarios)
- ✅ Docker image build
- ✅ Code quality checks

Pipeline status: `.github/workflows/ci-cd.yml`
