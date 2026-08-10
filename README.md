# genea-video-streamer

Open-source live video streaming solution built in C++ and LibAV, implemented as a Genea interview assessment.

## What It Does

- Ingests from IP cameras via RTSP (TCP or UDP transport)
- Remuxes compressed packets into HLS segments (no transcoding)
- Serves per-stream playlists and segments over HTTP
- Provides a browser-based HLS.js player for live view and archive playback
- Recovers automatically from network outages with exponential backoff
- Supports 1–N concurrent streams through an integrated stream manager
- Exposes a JSON REST API for aggregate and per-stream health metrics

## Architecture

```
RTSP Sources
  │
  ├─ StreamWorker #1 → RtspSource → PacketClock → HlsMuxer → segments/cam-1/
  ├─ StreamWorker #2 → RtspSource → PacketClock → HlsMuxer → segments/cam-2/
  └─ StreamWorker #N ...
  │
StreamManager (lifecycle, health aggregation)
  │
HttpServer
  ├─ GET /hls/<stream>/live.m3u8       → live playlist (rolling 3-5 segments)
  ├─ GET /hls/<stream>/archive.m3u8    → archive playlist (2-hour retention)
  ├─ GET /hls/<stream>/<seg>.ts        → segment (MPEG-TS video)
  ├─ GET /api/health                   → aggregate JSON metrics
  ├─ GET /api/streams                  → stream list with status
  ├─ GET /api/streams/<name>           → per-stream status
  ├─ GET /api/config                   → playback latency config
  └─ GET /                             → web player (HLS.js + stream selector)
```

## Implementation Status

| Phase | Name | Status |
|-------|------|--------|
| 0 | Foundation (CMake, config, logging) | ✅ Complete |
| 1 | Capture & Probe (RTSP, PacketClock) | ✅ Complete |
| 2 | Remux & Segments (HLS muxer, playlists, archive) | ✅ Complete |
| 3 | Web Player (HLS.js, live + archive UI, stream selector) | ✅ Complete |
| 4 | Reliability (reconnect, backoff, stale detection, idempotent shutdown) | ✅ Complete |
| 5 | Testing & CI (unit, integration, GitHub Actions, Docker) | ✅ Complete |
| 6 | Scalability (StreamManager, StreamWorker, HTTP API, multi-stream) | ✅ Complete |
| 6b | Playback Latency (configurable buffering, low-latency profiles) | ✅ Complete |
| 7 | AI / Optional (object detection, event search) | 📅 Not started |

## Tech Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++17 | Performance, LibAV compatibility |
| Media | LibAV (libavformat 60, libavcodec 60, libavutil 58) | Packet demux/remux without decode |
| Config | yaml-cpp 0.8 | Human-readable multi-stream config |
| Protocol | HLS (MPEG-TS segments) | Open standard, browser-native via HLS.js |
| HTTP | POSIX sockets (custom, no external dependency) | Zero added deps for test portability |
| Build | CMake 3.16+, pkg-config | Standard C++ build tooling |
| CI | GitHub Actions + Docker multi-stage | Reproducible builds and deployment |

## Quick Start

### Build

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Single-stream mode

```bash
# Edit config/rtsp-ingest.yaml with your camera URL, then:
./build/streamer config/rtsp-ingest.yaml
```

Open `http://localhost:8000` to view the player.

### Multi-stream mode

```bash
# Edit config/rtsp-multi-stream.yaml or config/multi-stream-example.yaml
./build/streamer config/rtsp-multi-stream.yaml
```

Streams are served at `/hls/<stream-name>/live.m3u8`.

### Tests

```bash
# Unit tests
./build/test_reconnect_policy
./build/test_pipeline_health
./build/test_http_server

# Integration tests
bash tests/integration/run_integration_tests.sh

# Full end-to-end suite (10 suites, 35+ scenarios)
bash tests/e2e/run_comprehensive_e2e.sh
```

### Docker

```bash
# Build image
docker build -t genea-streamer:latest .

# Run with docker-compose (edit docker-compose.yml for your RTSP URL first)
docker-compose up -d
docker-compose logs -f streamer
docker-compose down
```

## Configuration

### Single stream (`config/rtsp-ingest.yaml`)

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

hls:
  output_dir: "segments"
  segment_duration_s: 4
  enable_discontinuity_markers: true
```

### Multi-stream (`config/multi-stream-example.yaml`)

```yaml
http:
  listen_port: 8080
  listen_address: "0.0.0.0"
  enable_cors: true

streams:
  - name: "camera-front"
    rtsp:
      url: "rtsp://192.168.1.100:554/stream1"
      reconnect:
        enabled: true
        initial_delay_ms: 1000
        max_delay_ms: 30000
        stale_timeout_s: 10
    hls:
      segment_duration_s: 4
      output_dir: "segments/camera-front"

  - name: "camera-rear"
    rtsp:
      url: "rtsp://192.168.1.101:554/stream1"
      reconnect:
        enabled: true
        initial_delay_ms: 1000
        max_delay_ms: 30000
        stale_timeout_s: 10
    hls:
      segment_duration_s: 4
      output_dir: "segments/camera-rear"
```

## REST API

All endpoints return JSON. CORS is enabled by default.

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | Aggregate metrics across all streams |
| `/api/streams` | GET | List of all configured streams with status |
| `/api/streams/<name>` | GET | Per-stream status and metrics |
| `/hls/<name>/live.m3u8` | GET | Live HLS playlist for a stream |
| `/hls/<name>/<seg>.ts` | GET | HLS segment |
| `/` | GET | Web player UI |

Example health response:

```json
{
  "total_packets_read": 12480,
  "total_packets_written": 12480,
  "total_packets_dropped": 0,
  "total_reconnects": 1,
  "active_streams": 2,
  "error_streams": 0,
  "stream_stats": [
    { "name": "camera-front", "status": "1", "packets_written": 6240, "reconnects": 0 },
    { "name": "camera-rear",  "status": "1", "packets_written": 6240, "reconnects": 1 }
  ]
}
```

## Camera Compatibility

- **Recommended:** H.264 with regular IDR keyframes (every 1–2 s)
- **Transport:** TCP preferred (more reliable across NAT/firewalls)
- **Codec handling:** packets are remuxed without decode — no transcoding, no re-encoding
- **Browser playback:** depends on source codec; H.264 is universally supported

## CI/CD

GitHub Actions runs on every push:

- Build and compile (CMake, Release)
- Unit tests (ReconnectPolicy, PipelineHealth, HttpServer — 35+ tests)
- Integration tests (reconnect scenarios)
- Docker image build

Pipeline: `.github/workflows/ci-cd.yml`

## Design Document

Full technical design, architecture decisions, module breakdown, and testing matrix: [docs/design.md](docs/design.md)
