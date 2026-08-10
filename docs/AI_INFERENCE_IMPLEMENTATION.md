# AI Inference Module - Implementation Guide

## Overview

This document describes the AI Inference module for **genea-video-streamer**, which enables real-time object detection (person/car) on HLS video streams using YOLOv8.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  HLS Video Stream (H.264 @ 30fps)                               │
│  Input: RTSP → C++ Capture → HLS Segments (/app/hls_output/)   │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       │ segment_*.ts files (2 seconds each)
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│  AI Inference Worker (Python)                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ HlsReader: Track new segments from .m3u8 manifest        │   │
│  │ FrameExtractor: Extract frames from .ts at 1/segment     │   │
│  │ YoloDetector: Run YOLOv8n on frames (CPU, ~15-25 FPS)    │   │
│  │ FrameAnnotator: Draw bboxes + confidence labels          │   │
│  │ DetectionDatabase: Store results in SQLite + disk cache  │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   SQLite DB      Frame Files    HTTP API
   /app/        /app/detections/ /api/detections/
   detections.db  /camera-1/       *
```

## Key Components

### 1. Config Module (`config.py`)

Loads and validates configuration from YAML.

```python
from ai_inference.config import load_config, get_default_config

# Load from YAML
config = load_config('config/ai_inference_config.yaml')

# Or use defaults
config = get_default_config()

# Access settings
print(config.model)              # 'yolov8n'
print(config.confidence_threshold)  # 0.5
print(config.inference_interval_s)  # 5
```

### 2. Database Module (`database.py`)

SQLite wrapper for persistent detection storage.

```python
from ai_inference.database import DetectionDatabase

db = DetectionDatabase('/app/detections.db')

# Insert detection
det_id = db.insert_detection({
    'object_type': 'person',
    'confidence': 0.95,
    'bbox_x': 0.1, 'bbox_y': 0.2, 'bbox_w': 0.3, 'bbox_h': 0.4,
    'unix_timestamp': int(time.time()),
    'stream_id': 'camera-1'
})

# Query detections
recent = db.get_detections_by_time(start_ts, end_ts)
persons = db.get_detections_by_object_type('person', limit=100)
stats = db.get_statistics()

# Cleanup old records (30+ days)
deleted = db.cleanup_old_records(retention_days=30)

db.close()
```

**Schema:**

- `detections` table: object_type, confidence, bbox (normalized 0-1), timestamp, segment info, frame paths
- `detection_stats` table: hourly aggregation
- Indexes on: object_type, timestamp, confidence, segment_index
- WAL mode enabled for concurrent access

### 3. Detection Data Classes (`detection.py`)

```python
from ai_inference.detection import Detection, SegmentInfo

# Create detection object
det = Detection(
    object_type='person',
    confidence=0.92,
    bbox_x=0.1, bbox_y=0.2, bbox_w=0.3, bbox_h=0.4,
    unix_timestamp=1234567890,
    segment_filename='segment_000001.ts',
    stream_id='camera-1'
)

# Access/modify
det.frame_path_annotated = '/app/detections/camera-1/annotated/frame_1234567890_person.jpg'
det_dict = det.to_dict()  # Convert to DB-ready dict
```

### 4. Frame Annotation (`frame_annotator.py`)

Draws bounding boxes with labels on frames.

```python
from ai_inference.frame_annotator import FrameAnnotator
import cv2

annotator = FrameAnnotator(thickness=2, font_scale=0.6)

# Load frame
frame = cv2.imread('frame.jpg')  # BGR format

# Annotate with detections
annotated = annotator.annotate_frame(
    frame,
    detections=[det1, det2],
    draw_confidence=True
)

# Save result
annotator.save_frame(annotated, 'frame_annotated.jpg', quality=85)
```

**Colors:**
- Person: Green (0, 255, 0 in BGR)
- Car: Blue (255, 0, 0 in BGR)

### 5. Frame Extraction (`frame_extractor.py`)

Extracts frames from MPEG-TS (.ts) segments.

```python
from ai_inference.frame_extractor import FrameExtractor

extractor = FrameExtractor()

# Extract single frame at timestamp
frame = extractor.extract_frame_at_time(
    '/app/hls_output/camera-1/segment_000001.ts',
    time_s=1.0  # 1 second into segment
)

# Or extract frames at regular interval
frames = extractor.extract_frames_interval(
    '/app/hls_output/camera-1/segment_000001.ts',
    interval_s=0.5  # Every 0.5 seconds
)

# Get segment duration
duration = extractor.get_segment_duration(ts_file)
```

### 6. YOLOv8 Detector (`detector.py`)

Wraps YOLOv8 model with COCO class mapping.

```python
from ai_inference.detector import YoloDetector
import cv2

detector = YoloDetector(
    model_size='n',           # nano (6.3MB, ~15-25 FPS on CPU)
    classes=['person', 'car'],
    device='cpu'              # CPU only, no GPU required
)

# Load frame
frame = cv2.imread('frame.jpg')

# Run inference
detections = detector.detect(
    frame,
    confidence_threshold=0.5
)

# Benchmark
stats = detector.benchmark(num_iterations=100)
print(f"FPS: {stats['fps']}")
print(f"Latency: {stats['avg_latency_ms']} ms")
```

**COCO Class Mapping:**
- 0 → 'person'
- 2, 5, 7 → 'car' (car, bus, truck)

### 7. HLS Reader (`hls_reader.py`)

Parses HLS manifests and tracks segment availability.

```python
from ai_inference.hls_reader import HlsReader

reader = HlsReader('/app/hls_output/camera-1')

# Parse manifest
segments = reader.read_manifest('archive.m3u8')
for seg in segments:
    print(f"Segment {seg.index}: {seg.filename}")

# Get latest segment
latest = reader.get_latest_segment()

# Wait for new segment (blocking)
new_seg = reader.wait_for_new_segment(timeout_s=30)
```

### 8. Detection Worker (`detection_worker.py`)

Main inference loop running in background thread.

```python
from ai_inference.detection_worker import DetectionWorker
from ai_inference.config import load_config

config = load_config('config/ai_inference_config.yaml')
worker = DetectionWorker(config)

# Start in background thread
worker.start()

# Get statistics
stats = worker.get_statistics()
print(f"Total detections: {stats['total_detections']}")

# Stop gracefully
worker.stop()
```

**Processing Loop:**
1. Wait for new HLS segment
2. Extract frame at interval (default: middle of segment)
3. Run YOLOv8 inference
4. Annotate frame with bboxes
5. Save annotated frame to disk
6. Store detection record in database
7. Repeat

### 9. Search API (`search_api.py`)

Query interface and HTTP handlers.

```python
from ai_inference.search_api import DetectionSearchAPI, HttpDetectionHandler

# Query interface
api = DetectionSearchAPI('/app/detections.db')

# Search by time range
detections = api.search_by_time_range(start_ts, end_ts)

# Search by object type
persons = api.search_by_object_type('person', limit=100)

# Get recent
recent = api.search_recent(hours=1, limit=50)

# Statistics
stats = api.get_statistics()
hourly = api.get_hourly_statistics(hours=24)

# HTTP handler (for C++ integration)
handler = HttpDetectionHandler(
    '/app/detections.db',
    '/app/detections'
)

status, response = handler.handle_search_query(
    'recent',
    {'hours': 1, 'limit': 100}
)
```

## Directory Structure

```
genea-video-streamer/
├── src/ai_inference/           # Python module
│   ├── __init__.py
│   ├── config.py
│   ├── database.py
│   ├── detection.py
│   ├── frame_annotator.py
│   ├── frame_extractor.py
│   ├── detector.py
│   ├── hls_reader.py
│   ├── detection_worker.py
│   └── search_api.py
├── config/
│   └── ai_inference_config.yaml
├── detections/
│   ├── camera-1/
│   │   ├── annotated/         # Frames with bboxes
│   │   └── raw/               # Optional: raw frames
│   └── .gitignore             # Ignore frame files
├── tests/
│   ├── test_database.py
│   ├── test_frame_annotator.py
│   ├── test_detector.py
│   └── test_frame_extractor.py
├── web/
│   └── detections.html        # Detection gallery UI
└── requirements.txt           # Python dependencies
```

## Configuration

**File:** `config/ai_inference_config.yaml`

```yaml
ai_inference:
  # Core
  enabled: true
  model: yolov8n
  classes: [person, car]
  confidence_threshold: 0.5
  
  # Inference
  inference_interval_s: 5
  frames_per_segment: 1
  
  # Paths
  hls_input_dir: /app/hls_output/{stream_id}
  database_path: /app/detections.db
  detections_output_dir: /app/detections
  save_raw_frames: false
  
  # Performance
  max_workers: 4
  batch_size: 1
  device: cpu
  
  # Retention
  retention_days: 30
  
  # Annotation
  annotate_thickness: 2
  annotate_font_scale: 0.6
  annotation_quality: 85
  
  # Logging
  log_level: INFO
  log_detections: true
  
  # Stream
  stream_id: camera-1
```

## HTTP API Endpoints

Implemented in C++ (`src/server/HttpServer.cpp`):

### Detection Statistics

```
GET /api/detections/stats
```

Response:
```json
{
  "total_detections": 1234,
  "by_type": {"person": 890, "car": 344},
  "average_confidence": 0.87
}
```

### Recent Detections

```
GET /api/detections/recent?hours=1&limit=100
```

Response:
```json
{
  "detections": [
    {
      "id": 123,
      "object_type": "person",
      "confidence": 0.95,
      "unix_timestamp": 1234567890,
      "segment_filename": "segment_000042.ts",
      "frame_path_annotated": "/app/detections/camera-1/annotated/frame_1234567890_person.jpg"
    }
  ],
  "count": 42
}
```

### Detection Frame

```
GET /detections/frame/<detection_id>
```

Returns: JPEG image with bounding boxes drawn

## Web UI

**URL:** `http://localhost:8000/web/detections.html`

Features:
- Detection gallery with thumbnails
- Filter by object type (person/car)
- Filter by confidence threshold
- Time range selection (1h, 6h, 24h, week)
- Real-time statistics
- Click to view full resolution with metadata
- Pagination (20 items/page)

## Performance Characteristics

### YOLOv8n on CPU

- **Model Size:** 6.3 MB
- **Latency:** ~40-65 ms per frame (1280×720 input)
- **Throughput:** ~15-25 FPS on modern CPU (4+ cores)
- **Memory:** ~200-300 MB peak usage
- **CPU Load:** ~1-2 cores fully utilized

### Database

- **Storage:** ~100-200 KB per 1000 detections (metadata only, no images)
- **Queries:** <10ms for recent detections
- **Concurrent Access:** WAL mode allows concurrent read/write

### Disk Usage

- **Frame Storage:** ~50-100 KB per annotated frame (JPEG @ quality 85)
- **30-day Retention:** ~8-15 MB for typical traffic
- **Raw Frames:** Optional, ~300 KB each if enabled

## Deployment

### Prerequisites

```bash
# Install Python dependencies
pip install -r requirements.txt

# Verify installation
python3 -c "from ai_inference.detector import YoloDetector; print('OK')"
```

### Docker Integration

In `Dockerfile`, add Python layer:

```dockerfile
# Install Python dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy AI inference module
COPY src/ai_inference /app/ai_inference/
COPY config/ai_inference_config.yaml /app/config/
```

### Running Detection Worker

```bash
# As subprocess from C++ application
python3 -m ai_inference.detection_worker \
  --config config/ai_inference_config.yaml

# Or in separate container
docker run ... genea-video-streamer python3 -m ai_inference.detection_worker
```

## Testing

### Unit Tests

```bash
# Database
PYTHONPATH=src python3 tests/test_database.py

# Frame Annotator (requires opencv)
PYTHONPATH=src python3 tests/test_frame_annotator.py

# Detector (requires ultralytics)
PYTHONPATH=src python3 tests/test_detector.py

# Frame Extractor
PYTHONPATH=src python3 tests/test_frame_extractor.py
```

### Integration Testing

Once running in container with HLS segments available:

```bash
# Check database
sqlite3 /app/detections.db \
  "SELECT COUNT(*) FROM detections;"

# Check frame files
ls -lh /app/detections/camera-1/annotated/

# Test HTTP endpoints
curl http://localhost:8000/api/detections/stats
curl http://localhost:8000/api/detections/recent?hours=1
```

## Troubleshooting

### No Detections Being Recorded

1. **Check HLS output exists:**
   ```bash
   ls /app/hls_output/camera-1/
   ```

2. **Verify worker is running:**
   ```bash
   ps aux | grep detection_worker
   ```

3. **Check logs:**
   ```bash
   tail -f /var/log/genea-streamer.log
   ```

4. **Test config loading:**
   ```bash
   PYTHONPATH=src python3 -c \
    "from ai_inference.config import load_config; \
     cfg = load_config('config/ai_inference_config.yaml'); \
     print(cfg)"
   ```

### High CPU Usage

- Reduce `inference_interval_s` (fewer frames per second)
- Use larger model size (trades accuracy for speed)
- Check for lock contention on database

### Out of Memory

- Disable `save_raw_frames`
- Reduce `retention_days`
- Monitor frame disk usage: `du -sh /app/detections/`

## Future Enhancements

1. **Multi-stream support:** Process multiple camera feeds in parallel
2. **Custom models:** Support for fine-tuned YOLO models
3. **Real-time alerts:** Trigger actions on detection (webhook, email)
4. **Object tracking:** Track individuals/vehicles across frames
5. **Advanced analytics:** Heatmaps, crowd density estimation
6. **GPU acceleration:** CUDA/TensorRT for higher throughput

## License

See root project LICENSE file.
