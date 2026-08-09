# Phase 6: Multi-Stream Scalability Design

## Overview
Extend genea-video-streamer from single-stream to multi-stream architecture, supporting 5-10+ concurrent RTSP streams with proper resource management, health monitoring, and HLS output organization.

## Architecture Changes

### Current (Single-Stream)
```
Config → RtspSource → HlsMuxer → HTTP Server
                ↓
          PipelineHealth
```

### Proposed (Multi-Stream)
```
Config (with stream array)
    ↓
StreamManager (manages pool of streams)
    ├─→ StreamWorker #1 → RtspSource #1 → HlsMuxer #1
    ├─→ StreamWorker #2 → RtspSource #2 → HlsMuxer #2
    └─→ StreamWorker #N → RtspSource #N → HlsMuxer #N
    
    Aggregator → AggregateHealth → HTTP Server
```

## Key Components

### 1. StreamManager (NEW)
- **File:** `include/streamer/StreamManager.h`, `src/util/StreamManager.cpp`
- **Responsibility:** Manage lifecycle of multiple streams
- **Features:**
  - Stream creation/destruction
  - Resource pooling (thread pool for stream workers)
  - Health aggregation across streams
  - Graceful shutdown coordination

### 2. StreamWorker (NEW)
- **File:** `include/streamer/StreamWorker.h`, `src/util/StreamWorker.cpp`
- **Responsibility:** Encapsulate single-stream pipeline
- **Features:**
  - Independent RTSP source and HLS muxer
  - Per-stream health tracking
  - Isolated reconnect policy
  - Thread-safe operation

### 3. StreamConfig (MODIFIED)
- **Current:** Single RTSP URL in config
- **New:** Array of stream configurations
- **Structure:**
  ```yaml
  streams:
    - name: "camera-1"
      rtsp_url: "rtsp://camera1:554/stream"
      hls_output: "segments/camera1/"
      reconnect:
        enabled: true
        initial_delay_ms: 1000
        max_delay_ms: 30000
        jitter_percent: 15
        stale_timeout_s: 10
    
    - name: "camera-2"
      rtsp_url: "rtsp://camera2:554/stream"
      hls_output: "segments/camera2/"
      # Same reconnect config
  
  # Global settings
  http:
    listen_port: 8000
  
  # Resource limits
  resources:
    max_streams: 10
    max_cpu_per_stream: 1.0
    max_memory_per_stream: 512M
  ```

### 4. AggregateHealth (NEW)
- **File:** `include/streamer/AggregateHealth.h`, `src/util/AggregateHealth.cpp`
- **Responsibility:** Aggregate metrics from all streams
- **Metrics:**
  - Total packets read/written/dropped
  - Overall throughput (Mbps)
  - Stream-specific health status (up/down/reconnecting)
  - Last frame timestamp per stream
  - System uptime

### 5. HTTP Server Changes (MINOR)
- **Current:** Serves `segments/live.m3u8` and `segments/*.ts`
- **New:** Serves per-stream playlists
  - `/hls/camera1/live.m3u8`
  - `/hls/camera1/segment-*.ts`
  - `/hls/camera2/live.m3u8`
  - `/hls/camera2/segment-*.ts`
  - `/api/health` → returns aggregate metrics (JSON)

## Implementation Plan

### Phase 6.1: Config & Infrastructure
1. Create `StreamConfig` struct with array of stream definitions
2. Update `Config.h` and `Config.cpp` to parse multi-stream YAML
3. Create `StreamManager` skeleton
4. Add integration tests for config parsing

### Phase 6.2: Stream Worker
1. Implement `StreamWorker` encapsulating single-stream pipeline
2. Create per-stream health tracking
3. Add thread-safe queue management
4. Test isolated stream operation

### Phase 6.3: Stream Manager
1. Implement thread pool for stream workers
2. Add stream lifecycle management (create/start/stop/destroy)
3. Implement health aggregation
4. Add graceful shutdown protocol

### Phase 6.4: Multi-Stream HLS
1. Update `HlsMuxer` to support per-stream output directories
2. Modify segment naming for stream isolation
3. Generate per-stream playlists
4. Update playlist path logic

### Phase 6.5: HTTP & API
1. Update HTTP server routing for multi-stream
2. Add `/api/health` endpoint (JSON)
3. Serve per-stream playlists and segments
4. Add stream list endpoint `/api/streams`

### Phase 6.6: Testing & Validation
1. Create multi-stream E2E tests
2. Test concurrent reconnects
3. Stress test with 5-10 streams
4. Verify health aggregation
5. Benchmark resource usage

## Backward Compatibility

Support both single-stream (legacy) and multi-stream configs:
- If `rtsp.url` exists → single-stream mode (current behavior)
- If `streams` array exists → multi-stream mode
- Auto-migrate config if needed

## Resource Considerations

- **Memory:** ~50-100MB per stream (source buffer + HLS state)
- **CPU:** ~0.5-1 core per stream depending on codec
- **Disk I/O:** ~1-5 Mbps per stream
- **Network I/O:** 1:1 with stream bitrate

For 10 streams @ 5Mbps each:
- Memory: ~1GB
- CPU: 5-10 cores
- Disk: 50Mbps
- Network: 50Mbps

## Success Criteria

✅ Support 5 concurrent streams simultaneously
✅ Each stream independent reconnect/recovery
✅ Aggregate health metrics accessible via HTTP
✅ Per-stream HLS playlists working
✅ Resource usage predictable and bounded
✅ Graceful degradation (drop one stream → others unaffected)
✅ Backward compatible with single-stream config
✅ E2E tests validate multi-stream scenarios

## Timeline

- Phase 6.1: ~1 hour
- Phase 6.2: ~1.5 hours
- Phase 6.3: ~1.5 hours
- Phase 6.4: ~1 hour
- Phase 6.5: ~1 hour
- Phase 6.6: ~1 hour

**Total: ~7 hours**
