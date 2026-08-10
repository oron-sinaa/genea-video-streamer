# HTTP API Reference

This document describes all HTTP endpoints available on the Genea Video Streamer HTTP server (default port 8080).

## Overview

- **Base URL:** `http://<host>:8080`
- **Methods Supported:** `GET`, `HEAD`
- **Content Types:** JSON (for API endpoints), MPEGURL (for playlists), video/mp2t (for segments), HTML (for web player)
- **CORS Support:** Configurable via `enable_cors` setting

## Endpoints

### Health & Status Endpoints

#### 1. Get Aggregate Health
**Request:**
```
GET /api/health
```

**Description:**  
Returns comprehensive health metrics for all active streams combined, including aggregate statistics and per-stream details.

**Response (200 OK):**
```json
{
  "total_packets_read": 1500,
  "total_packets_written": 1500,
  "total_packets_dropped": 0,
  "total_reconnects": 2,
  "total_throughput_mbps": 12.5,
  "active_streams": 2,
  "error_streams": 0,
  "stream_stats": [
    {
      "name": "camera-1",
      "status": 2,
      "packets_written": 750,
      "throughput_mbps": 6.25,
      "reconnects": 1
    },
    {
      "name": "camera-2",
      "status": 2,
      "packets_written": 750,
      "throughput_mbps": 6.25,
      "reconnects": 1
    }
  ]
}
```

**Status Codes:**
- `200 OK` - Health metrics returned successfully
- `404 Not Found` - StreamManager not initialized

**Notes:**
- `status` field is an integer: 0=IDLE, 1=RUNNING, 2=RECONNECTING, 3=ERROR
- Metrics are updated in real-time
- `throughput_mbps` represents average bitrate

---

#### 2. List All Streams
**Request:**
```
GET /api/streams
```

**Description:**  
Returns a list of all configured streams with their current status and statistics.

**Response (200 OK):**
```json
{
  "total_streams": 2,
  "streams": [
    {
      "name": "camera-1",
      "status": 2,
      "packets_written": 750,
      "reconnects": 1
    },
    {
      "name": "camera-2",
      "status": 2,
      "packets_written": 750,
      "reconnects": 1
    }
  ]
}
```

**Status Codes:**
- `200 OK` - Stream list returned successfully
- `404 Not Found` - StreamManager not initialized

**Notes:**
- Returns empty array if no streams are configured
- `status` is stream status code (0=IDLE, 1=RUNNING, 2=RECONNECTING, 3=ERROR)

---

#### 3. Get Stream Status
**Request:**
```
GET /api/streams/<stream-name>
```

**Description:**  
Returns detailed status and metrics for a specific stream.

**URL Parameters:**
- `<stream-name>` (string) - Name of the stream as configured in the config file

**Response (200 OK):**
```json
{
  "name": "camera-1",
  "status": 2,
  "packets_written": 750,
  "throughput_mbps": 6.25,
  "reconnects": 1
}
```

**Status Codes:**
- `200 OK` - Stream status returned successfully
- `404 Not Found` - Stream not found or StreamManager not initialized

**Example:**
```bash
curl http://localhost:8080/api/streams/camera-1
```

---

#### 4. Get Playback Configuration
**Request:**
```
GET /api/config
```

**Description:**  
Returns playback latency and buffering configuration for HLS.js client-side settings. Used by the web player to configure optimal latency profiles.

**Response (200 OK):**
```json
{
  "playback": {
    "live_mode": {
      "back_buffer_length_s": 10,
      "sync_segment_count": 2,
      "max_buffer_length_s": 30,
      "max_buffer_length_absolute_s": 60
    }
  }
}
```

**Status Codes:**
- `200 OK` - Configuration returned successfully
- `404 Not Found` - StreamManager not initialized

**Response Fields:**
- `back_buffer_length_s` (int) - Seconds of buffer to maintain behind live edge (lower = lower latency)
- `sync_segment_count` (int) - Number of segments ahead to sync toward (controls live position aggressiveness)
- `max_buffer_length_s` (int) - Maximum total buffer duration in seconds
- `max_buffer_length_absolute_s` (int) - Hard ceiling on buffer (safety limit)

**Notes:**
- Configuration is read from `playback.live_mode` section in YAML config file
- Default values provide balanced latency (~10-15s) with good resilience
- Can be tuned for low-latency (5-8s) or high-reliability (20-30s) profiles
- Settings are applied dynamically by the player without requiring restart

**Example:**
```bash
curl http://localhost:8080/api/config
```

---

### HLS Streaming Endpoints

#### 5. Get Live HLS Playlist
**Request:**
```
GET /hls/<stream-name>/live.m3u8
```

**Description:**  
Returns the HLS live playlist for a stream. Contains references to available segments in a rolling window with segment duration and timing information.

**URL Parameters:**
- `<stream-name>` (string) - Name of the stream (e.g., camera-1)

**Response (200 OK):**
```
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:4
#EXT-X-MEDIA-SEQUENCE:1000
#EXTINF:3.996,
segment_001000.ts
#EXTINF:3.996,
segment_001001.ts
#EXTINF:3.996,
segment_001002.ts
```

**Response Headers:**
```
Content-Type: application/vnd.apple.mpegurl
Content-Length: <size>
```

**Status Codes:**
- `200 OK` - Playlist returned successfully
- `404 Not Found` - Stream not found or playlist file doesn't exist

**Notes:**
- Playlist is updated as new segments are created
- Rolling window includes most recent segments (typically 3-5 segments)
- No ENDLIST tag (maintains live stream state)
- Includes #EXT-X-DISCONTINUITY markers after reconnects if enabled in config
- Compatible with HLS.js and standard media players

**Example:**
```bash
curl http://localhost:8080/hls/camera-1/live.m3u8
```

---

#### 6. Get Archive HLS Playlist
**Request:**
```
GET /hls/<stream-name>/archive.m3u8
```

**Description:**  
Returns the HLS archive playlist for a stream containing the complete recorded history (up to retention limit). Used for playback and seeking to past content.

**URL Parameters:**
- `<stream-name>` (string) - Name of the stream (e.g., camera-1)

**Response (200 OK):**
```
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:4
#EXT-X-MEDIA-SEQUENCE:900
#EXTINF:3.996,
segment_000900.ts
#EXTINF:3.996,
segment_000901.ts
...
#EXTINF:3.996,
segment_001002.ts
```

**Response Headers:**
```
Content-Type: application/vnd.apple.mpegurl
Content-Length: <size>
```

**Status Codes:**
- `200 OK` - Playlist returned successfully
- `404 Not Found` - Stream not found or playlist file doesn't exist

**Notes:**
- Archive contains all segments within the configured retention period (default: 2 hours)
- Retention duration is configurable via `hls.archive_retention_hours` in config
- Segments older than retention period are automatically cleaned up
- Supports full seek/playback capability
- Can contain hundreds or thousands of segments depending on bitrate and retention

**Example:**
```bash
curl http://localhost:8080/hls/camera-1/archive.m3u8
```

---

#### 7. Get HLS Segment
**Request:**
```
GET /hls/<stream-name>/<segment-name>.ts
```

**Description:**  
Returns a single MPEG-TS video segment file for download and playback.

**URL Parameters:**
- `<stream-name>` (string) - Name of the stream
- `<segment-name>` (string) - Segment filename (e.g., segment_000001)

**Response (200 OK):**
```
Binary MPEG-TS video data
```

**Response Headers:**
```
Content-Type: video/mp2t
Content-Length: <size>
```

**Status Codes:**
- `200 OK` - Segment returned successfully
- `404 Not Found` - Stream or segment file not found

**Notes:**
- Segments are H.264 video in MPEG-TS container format
- Typically 3-10 MB per segment depending on bitrate and duration
- Segments are retained according to `archive_retention_hours` config

**Example:**
```bash
curl http://localhost:8080/hls/camera-1/segment_000001.ts -o segment.ts
```

---

### Web Interface

#### 9. Get Web Player
**Request:**
```
GET /
GET /index.html
```

**Description:**  
Returns the HTML web player interface for viewing HLS streams in a browser. Includes stream selector for multi-stream setups and controls for switching between live and archive modes.

**Response (200 OK):**
```html
<!DOCTYPE html>
<html>
  <head>
    <title>Genea Video Streamer</title>
    ...
  </head>
  <body>
    <!-- HLS.js player with stream selector and controls -->
  </body>
</html>
```

**Response Headers:**
```
Content-Type: text/html
Content-Length: <size>
```

**Status Codes:**
- `200 OK` - Player page returned successfully
- `404 Not Found` - Player HTML file not available (fallback page served)

**Features:**
- Real-time stream selection dropdown (multi-stream mode)
- Live vs Archive playback toggle
- Play/pause, volume, fullscreen controls
- Automatic stream status polling
- Dynamic latency configuration from server
- Graceful fallback if player.html not found

**Notes:**
- Uses HLS.js library for playback
- Requires modern browser with Media Source Extensions support
- Automatically loads configuration from `/api/config` endpoint
- Queries `/api/streams` every 5 seconds to keep stream selector updated

**Example:**
```bash
# Open in browser
curl http://localhost:8080/ > player.html
open player.html
```

---

## Configuration Reference

### Playback Latency Profiles

The `/api/config` endpoint returns settings that control client-side buffering. Common profiles:

**Low-Latency (5-8s total latency):**
```json
{
  "playback": {
    "live_mode": {
      "back_buffer_length_s": 5,
      "sync_segment_count": 1,
      "max_buffer_length_s": 20,
      "max_buffer_length_absolute_s": 40
    }
  }
}
```
Use for: Live sports, events, real-time monitoring requiring immediate reaction.
Trade-off: Higher risk of stalling on poor networks.

**Balanced (10-15s total latency - DEFAULT):**
```json
{
  "playback": {
    "live_mode": {
      "back_buffer_length_s": 10,
      "sync_segment_count": 2,
      "max_buffer_length_s": 30,
      "max_buffer_length_absolute_s": 60
    }
  }
}
```
Use for: Surveillance, security monitoring, general-purpose streaming.
Trade-off: Good balance between low latency and resilience.

**High-Reliability (20-30s total latency):**
```json
{
  "playback": {
    "live_mode": {
      "back_buffer_length_s": 15,
      "sync_segment_count": 3,
      "max_buffer_length_s": 50,
      "max_buffer_length_absolute_s": 100
    }
  }
}
```
Use for: Poor network conditions, remote locations, maximum resilience.
Trade-off: Higher latency but very stable playback.

---

#### 404 Not Found
**Response:**
```
404 Not Found
```

**Triggered by:**
- Invalid path
- Stream name doesn't exist
- Segment file doesn't exist
- Player HTML not found

---

#### 405 Method Not Allowed
**Response:**
```
Method not allowed
```

**Triggered by:**
- POST, PUT, DELETE, PATCH requests
- Only GET and HEAD are supported

---

## HTTP Methods

### Supported Methods
- **GET** - Retrieve resource (full response body)
- **HEAD** - Same as GET but without response body

### Unsupported Methods
- **POST, PUT, DELETE, PATCH** - Return `405 Method Not Allowed`

---

## Response Headers

All responses include:
- `Content-Type` - MIME type of response
- `Content-Length` - Size of response body in bytes

### Optional CORS Headers (when enabled)
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, HEAD
Access-Control-Allow-Headers: Content-Type
```

---

## Examples

### List all active streams
```bash
curl http://localhost:8080/api/streams | jq
```

### Get health metrics
```bash
curl http://localhost:8080/api/health | jq
```

### Get specific stream status
```bash
curl http://localhost:8080/api/streams/camera-1 | jq
```

### Check if stream is healthy
```bash
curl -I http://localhost:8080/api/health
# Returns 200 if healthy
```

### Download HLS playlist
```bash
curl http://localhost:8080/hls/camera-1/live.m3u8 -o playlist.m3u8
```

### Download specific segment
```bash
curl http://localhost:8080/hls/camera-1/segment_000001.ts -o segment.ts
```

### Play stream in browser
```
http://localhost:8080/
```
Then select stream from dropdown.

---

## Configuration

HTTP server behavior can be configured in the config file:

```yaml
http:
  listen_port: 8080          # Port to listen on
  enable_cors: true          # Enable CORS headers
  max_connections: 100       # Maximum concurrent connections
```

---

## Timestamps & Metrics

- **Packets Written** - Number of video packets successfully written to segments
- **Throughput (Mbps)** - Average bitrate in megabits per second
- **Reconnects** - Number of times RTSP connection was re-established
- **Status Codes:**
  - 0 = IDLE (not running)
  - 1 = RUNNING (active)
  - 2 = RECONNECTING (attempting to restore connection)
  - 3 = ERROR (unrecoverable error)

---

## Performance Notes

- Playlist requests are fast (read from disk)
- Segment requests are I/O bound (file size dependent)
- Health API queries are O(n) where n = number of streams
- Concurrent connections are handled sequentially (single-threaded listener)

---

## See Also

- [README.md](../README.md) - Project overview
- [design.md](./design.md) - Architecture and design details
- [Config documentation](./config.md) - Configuration reference
