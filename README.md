# genea-video-streamer

Open-source live video streaming and AI inference solution. Built in C++ (LibAV) for streaming, Python (YOLOv8) for object detection.

## What It Does

**Streaming (C++):**
- Ingests from IP cameras via RTSP (TCP or UDP transport)
- Remuxes compressed packets into HLS segments (no transcoding)
- Serves per-stream playlists and segments over HTTP
- Provides a browser-based HLS.js player for live view and archive playback
- Recovers automatically from network outages with exponential backoff
- Supports 1–N concurrent streams through an integrated stream manager
- Exposes a JSON REST API for stream health and status

**AI Inference (Python, Optional):**
- Runs object detection (YOLOv8 Nano) on HLS segments in real-time
- Detects persons and vehicles with normalized bounding boxes
- Stores detections in SQLite database with frame captures
- Saves annotated and raw frames to per-stream directories
- Supports multi-stream inference with independent workers
- Exposes REST API for detection queries, stats, and frame retrieval

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
| 7 | AI Inference (YOLOv8 detection, multi-stream workers, SQLite storage, detection API) | ✅ Complete |

## Tech Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| **Streaming** | | |
| Language | C++17 | Performance, LibAV compatibility |
| Media | LibAV (libavformat 60, libavcodec 60, libavutil 58) | Packet demux/remux without decode |
| Config | yaml-cpp 0.8 | Human-readable multi-stream config |
| Protocol | HLS (MPEG-TS segments) | Open standard, browser-native via HLS.js |
| HTTP | POSIX sockets (custom) | Zero added deps for test portability |
| **Inference (Optional)** | | |
| Language | Python 3.7+ | Rapid model integration, rich ecosystem |
| Model | YOLOv8 Nano | 6.3 MB, 40-65ms latency, 15-25 FPS on CPU |
| Frame Extraction | ffmpeg subprocess | Reliable MPEG-TS frame extraction |
| Database | SQLite (WAL mode) | Lightweight, concurrent-safe, no server |
| Configuration | PyYAML | Multi-stream config per-worker |
| **Deployment** | | |
| Container | Docker + docker-compose | Reproducible, full-stack deployment |
| Orchestration | docker-compose v2+ | Multi-service coordination with health checks |
| CI | GitHub Actions | Automated build, test, publish |

## Quick Start

### Deployment with Docker

```bash
# Clone/download the repository
git clone https://github.com/your-repo/genea-video-streamer.git
cd genea-video-streamer

# Edit config/rtsp-multi-stream.yaml with your RTSP camera URLs
# And config/inference.yaml to enable/disable AI detection per stream

# Start the full stack (streaming + optional AI inference)
docker-compose up -d

# View streaming: http://localhost:8080
# View detection stats: http://localhost:8080/api/detections/stats (if inference enabled)
# View stream health: http://localhost:8080/api/health

# Monitor logs
docker-compose logs -f streamer
docker-compose logs -f inference  # if enabled

# Stop services
docker-compose down
```

### Testing

Tests run automatically in the Docker build:

```bash
# Full end-to-end validation (includes build, unit tests, integration, Docker build)
bash tests/e2e/run_comprehensive_e2e.sh
```

## Configuration

### Streaming Config (`config/rtsp-multi-stream.yaml`)

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
```

### Inference Config (`config/inference.yaml`)

```yaml
ai_inference:
  model: yolov8n
  classes: [person, car]
  confidence_threshold: 0.5
  database_path: /app/detections.db
  device: cpu
  
  streams:
    - stream_id: camera-1
      enabled: true
      hls_input_dir: /data/hls_output/camera-1
      detections_output_dir: /data/detections/camera-1
    
    - stream_id: camera-2
      enabled: false  # Disabled but config preserved
      hls_input_dir: /data/hls_output/camera-2
      detections_output_dir: /data/detections/camera-2
```

## REST API

**Streaming Endpoints** (see [docs/http-api.md](docs/http-api.md) for full reference):

| Endpoint | Description |
|----------|-------------|
| `/api/health` | Aggregate stream metrics |
| `/api/streams` | Stream list and status |
| `/api/config` | Playback configuration |
| `/hls/<name>/live.m3u8` | Live HLS playlist |
| `/hls/<name>/<seg>.ts` | HLS segment file |
| `/` | Web player UI |

**Detection Endpoints** (if AI inference enabled):

| Endpoint | Description |
|----------|-------------|
| `/api/detections/stats` | Aggregate detection statistics (count, confidence, by type) |
| `/api/detections/recent?limit=20&stream_id=camera-1` | Recent detections with optional filtering |
| `/detections/frame/<frame_id>` | Retrieve annotated frame image |

Example:
```bash
curl http://localhost:8080/api/detections/stats | jq .
curl "http://localhost:8080/api/detections/recent?stream_id=camera-1" | jq .
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
