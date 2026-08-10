# AI Inference Implementation Plan (Revised)

## Metacognitive Overview

### Why Store Detected Frames?

**Key Insight**: Detection metadata alone (confidence, bbox coords) is not visually verifiable. The actual frame proves:
- ✅ Detection is real (not false positive)
- ✅ Bbox placement is correct
- ✅ Context visible (what surrounds person/car)
- ✅ Enables manual review + labeling
- ✅ Useful for training data collection

### Why NOT Store in Database?

**Problem 1: Database Bloat**
- 1000 detections × 100KB image = 100MB database file
- Queries slow down (SQLite not optimized for BLOBs)
- Backup/replication expensive

**Problem 2: Stream Serving**
- Hard to serve images directly from SQLite
- Web browsers expect HTTP paths, not BLOB extracts
- Caching inefficient

### Why Disk-Based + Reference in DB?

**Solution: Hybrid Approach**
```
Disk Structure:
  /app/detections/
  ├── camera-1/
  │   ├── frame_1723305600_person_0.85.jpg     ← Annotated
  │   ├── frame_1723305605_car_0.72.jpg
  │   └── raw/
  │       ├── frame_1723305600_person.jpg      ← Original (optional)
  │       └── frame_1723305605_car.jpg

Database:
  detections table
  ├── object_type: "person"
  ├── confidence: 0.85
  ├── bbox_x, bbox_y, bbox_w, bbox_h
  ├── frame_path: "/app/detections/camera-1/frame_1723305600_person_0.85.jpg"
  └── (reference via HTTP: http://localhost:8080/detections/frame_...)
```

**Why This Works:**
- ✅ Database remains lean (just paths)
- ✅ Frames served via HTTP (standard web pattern)
- ✅ Cleanup simple (delete old files + DB records)
- ✅ Inspection: view frame in browser, verify detection
- ✅ Aligned with existing HLS disk architecture
- ✅ Independent lifecycle from segments (7-day segments vs 30-day detection frames)

---

## Revised Architecture

### Directory Structure

```
/app/detections/
├── camera-1/
│   ├── annotated/
│   │   ├── frame_1723305600_person_0.85.jpg
│   │   ├── frame_1723305605_car_0.72.jpg
│   │   └── ...
│   └── raw/                      (optional, for re-training)
│       ├── frame_1723305600_person.jpg
│       ├── frame_1723305605_car.jpg
│       └── ...
├── camera-2/
│   └── annotated/
│       ├── frame_1723305600_person_0.91.jpg
│       └── ...
└── .gitignore                    (ignore all .jpg files)
```

### Database Schema (Revised)

```sql
CREATE TABLE detections (
    id INTEGER PRIMARY KEY,
    object_type TEXT NOT NULL,          -- 'person' or 'car'
    confidence REAL NOT NULL,            -- 0.0-1.0
    
    -- Bounding box (normalized 0-1, top-left origin)
    bbox_x REAL, bbox_y REAL,
    bbox_w REAL, bbox_h REAL,
    
    -- Time & segment
    unix_timestamp INTEGER NOT NULL,
    human_readable_time TEXT,
    segment_index INTEGER,
    segment_filename TEXT,
    frame_number_in_segment INTEGER,
    
    -- File references (NEW)
    frame_path_annotated TEXT,           -- /detections/camera-1/annotated/frame_1723305600_person_0.85.jpg
    frame_path_raw TEXT,                 -- /detections/camera-1/raw/frame_1723305600_person.jpg (optional)
    
    -- Source
    stream_id TEXT,
    
    -- Metadata
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);

CREATE TABLE detection_stats (
    id INTEGER PRIMARY KEY,
    stream_id TEXT,
    hour_bucket TEXT,
    person_count INTEGER DEFAULT 0,
    car_count INTEGER DEFAULT 0,
    total_detections INTEGER DEFAULT 0,
    avg_confidence REAL,
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);

-- Indexes
CREATE INDEX idx_detections_object_type ON detections(object_type);
CREATE INDEX idx_detections_timestamp ON detections(unix_timestamp);
CREATE INDEX idx_detections_confidence ON detections(confidence);
```

---

## Phase 1: Core Infrastructure (Enhanced)

### 1.1 File Structure

```
src/ai_inference/
├── __init__.py
├── config.py
├── database.py              # MODIFIED: Add frame path columns
├── detector.py
├── hls_reader.py
├── frame_extractor.py
├── frame_annotator.py       # NEW: Draw bboxes on frames
├── detection_worker.py       # MODIFIED: Save frames
└── search_api.py            # MODIFIED: Serve frames via HTTP

config/
└── ai_inference_config.yaml

detections/                   # NEW: Output directory
├── camera-1/
│   ├── annotated/
│   └── raw/
└── .gitignore
```

### 1.2 Dependencies (Revised)

```
ultralytics>=8.0.0    # YOLOv8
opencv-python         # Frame extraction + annotation
numpy
Pillow
```

---

## Phase 2: Core Modules (Revised)

### 2.1 `frame_annotator.py` (NEW)

**Purpose**: Draw bounding boxes on frames

```python
class FrameAnnotator:
    def __init__(self, thickness: int = 2, font_scale: float = 0.6):
        self.thickness = thickness
        self.font_scale = font_scale
        self.colors = {
            'person': (0, 255, 0),    # Green
            'car': (255, 0, 0),       # Blue
        }
    
    def annotate_frame(
        self, 
        frame: np.ndarray,
        detections: List[Detection],
        draw_confidence: bool = True
    ) -> np.ndarray:
        """
        Draw bboxes on frame.
        
        Args:
            frame: Input image (BGR, H×W×3)
            detections: List of Detection objects with bbox coords
            draw_confidence: Whether to show confidence scores
            
        Returns:
            Annotated frame (same shape as input)
        """
        output = frame.copy()
        height, width = frame.shape[:2]
        
        for det in detections:
            # Convert normalized coords (0-1) to pixel coords
            x1 = int(det.bbox_x * width)
            y1 = int(det.bbox_y * height)
            x2 = int((det.bbox_x + det.bbox_w) * width)
            y2 = int((det.bbox_y + det.bbox_h) * height)
            
            # Get color for class
            color = self.colors.get(det.object_type, (255, 255, 255))
            
            # Draw box
            cv2.rectangle(output, (x1, y1), (x2, y2), color, self.thickness)
            
            # Draw label
            label = det.object_type
            if draw_confidence:
                label += f" {det.confidence:.2f}"
            
            # Put text on box
            text_size = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, self.font_scale, self.thickness)[0]
            text_x = x1
            text_y = max(y1 - 5, text_size[1])
            
            # Background rectangle for text
            cv2.rectangle(output, 
                         (text_x, text_y - text_size[1] - 5),
                         (text_x + text_size[0] + 5, text_y + 5),
                         color, -1)
            
            # Text
            cv2.putText(output, label, (text_x, text_y),
                       cv2.FONT_HERSHEY_SIMPLEX, self.font_scale,
                       (0, 0, 0), self.thickness)
        
        return output
    
    def save_frame(self, frame: np.ndarray, path: str, quality: int = 85):
        """Save frame as JPEG with quality setting"""
        cv2.imwrite(path, frame, [cv2.IMWRITE_JPEG_QUALITY, quality])
```

### 2.2 `detection_worker.py` (Revised)

**New Behavior**: Save frames + update database

```python
class DetectionWorker:
    def __init__(self, config: Config):
        self.hls_reader = HlsReader(config.hls_input_dir)
        self.extractor = FrameExtractor()
        self.detector = YoloDetector(classes=config.classes)
        self.annotator = FrameAnnotator()
        self.db = DetectionDatabase(config.database_path)
        self.config = config
        
        # Create output directories
        self.detections_dir = Path(config.detections_output_dir)
        self.stream_dir = self.detections_dir / config.stream_id
        self.annotated_dir = self.stream_dir / "annotated"
        self.raw_dir = self.stream_dir / "raw"
        
        self.annotated_dir.mkdir(parents=True, exist_ok=True)
        if config.save_raw_frames:
            self.raw_dir.mkdir(parents=True, exist_ok=True)
    
    def process_segment(self, segment: SegmentInfo) -> List[Detection]:
        """
        1. Extract frame from segment
        2. Run YOLOv8 inference
        3. SAVE ANNOTATED FRAME (NEW)
        4. Optionally save raw frame (NEW)
        5. Store detection in database
        6. Return results
        """
        # Extract frame
        frame = self.extractor.extract_frame_at_time(
            segment.filepath, 
            segment.duration / 2  # Middle of segment
        )
        
        # Run detection
        detections = self.detector.detect(frame, self.config.confidence_threshold)
        
        if not detections:
            return []  # No detections in this frame
        
        # Get frame timestamp
        frame_ts = int(time.time())
        
        # Save annotated frame (NEW)
        annotated_frame = self.annotator.annotate_frame(frame, detections)
        annotated_path = self._get_frame_path(
            frame_ts, 
            detections[0].object_type,  # Use first detection type in filename
            annotated=True
        )
        self.annotator.save_frame(annotated_frame, str(annotated_path), quality=85)
        LOG_INFO(f"Saved annotated frame: {annotated_path}")
        
        # Save raw frame (optional)
        raw_path = None
        if self.config.save_raw_frames:
            raw_path = self._get_frame_path(frame_ts, detections[0].object_type, annotated=False)
            self.annotator.save_frame(frame, str(raw_path), quality=90)
            LOG_INFO(f"Saved raw frame: {raw_path}")
        
        # Store in database (NEW: include frame paths)
        for det in detections:
            det.frame_path_annotated = str(annotated_path)
            det.frame_path_raw = str(raw_path) if raw_path else None
            det.stream_id = self.config.stream_id
            det.unix_timestamp = frame_ts
            
            self.db.insert_detection(det)
        
        return detections
    
    def _get_frame_path(self, timestamp: int, object_type: str, annotated: bool = True) -> Path:
        """Generate frame filename"""
        subdir = self.annotated_dir if annotated else self.raw_dir
        filename = f"frame_{timestamp}_{object_type}.jpg"
        return subdir / filename
    
    def run_inference_loop(self):
        """Main loop: process segments every 5 seconds"""
        while True:
            try:
                segment = self.hls_reader.wait_for_new_segment(timeout_s=30)
                if segment is None:
                    continue
                
                detections = self.process_segment(segment)
                
                if detections:
                    LOG_INFO(f"Segment {segment.index}: {len(detections)} detections, frames saved")
                
                # Cleanup old frames (NEW)
                self._cleanup_old_frames()
                
            except Exception as e:
                LOG_ERROR(f"Detection error: {e}")
                time.sleep(5)
    
    def _cleanup_old_frames(self):
        """Delete frames older than retention policy"""
        retention_seconds = self.config.retention_days * 86400
        now = time.time()
        
        for frame_path in self.annotated_dir.glob("*.jpg"):
            mtime = frame_path.stat().st_mtime
            age_seconds = now - mtime
            
            if age_seconds > retention_seconds:
                frame_path.unlink()
                LOG_INFO(f"Deleted old frame: {frame_path}")
```

### 2.3 `database.py` (Revised)

```python
class DetectionDatabase:
    def insert_detection(self, detection: Detection) -> int:
        """Insert detection with frame paths"""
        cursor = self.conn.cursor()
        cursor.execute("""
            INSERT INTO detections (
                object_type, confidence,
                bbox_x, bbox_y, bbox_w, bbox_h,
                unix_timestamp, human_readable_time,
                segment_index, segment_filename,
                frame_number_in_segment,
                frame_path_annotated, frame_path_raw,
                stream_id
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            detection.object_type,
            detection.confidence,
            detection.bbox_x,
            detection.bbox_y,
            detection.bbox_w,
            detection.bbox_h,
            detection.unix_timestamp,
            datetime.fromtimestamp(detection.unix_timestamp).strftime('%Y-%m-%d %H:%M:%S'),
            detection.segment_index,
            detection.segment_filename,
            detection.frame_number_in_segment,
            detection.frame_path_annotated,
            detection.frame_path_raw,
            detection.stream_id
        ))
        self.conn.commit()
        return cursor.lastrowid
    
    def get_detection_frame_url(self, detection_id: int) -> Optional[str]:
        """Get HTTP-accessible URL for detection frame"""
        cursor = self.conn.cursor()
        cursor.execute("SELECT frame_path_annotated FROM detections WHERE id = ?", (detection_id,))
        row = cursor.fetchone()
        if row:
            # Convert /app/detections/... → http://localhost:8080/detections/...
            return row[0].replace('/app/', '/').lstrip('/')
        return None
```

### 2.4 `search_api.py` (Revised)

```python
class DetectionSearchAPI:
    def search_by_time(self, start_ts: int, end_ts: int) -> List[DetectionWithFrame]:
        """Get detections + frame URLs"""
        detections = self.db.get_detections_by_time(start_ts, end_ts)
        
        result = []
        for det in detections:
            det.frame_url = self.db.get_detection_frame_url(det.id)
            result.append(det)
        
        return result
    
    def serve_frame(self, detection_id: int) -> Optional[bytes]:
        """Serve frame image data (for HTTP endpoint)"""
        cursor = self.db.conn.cursor()
        cursor.execute("SELECT frame_path_annotated FROM detections WHERE id = ?", (detection_id,))
        row = cursor.fetchone()
        
        if row:
            frame_path = row[0]
            with open(frame_path, 'rb') as f:
                return f.read()
        
        return None
    
    def export_with_frames(self, start_ts: int, end_ts: int, output_dir: str):
        """Export detections + copies of frames to directory"""
        detections = self.db.get_detections_by_time(start_ts, end_ts)
        
        export_path = Path(output_dir)
        export_path.mkdir(parents=True, exist_ok=True)
        
        # CSV metadata
        with open(export_path / 'detections.csv', 'w') as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=[
                'id', 'object_type', 'confidence',
                'bbox_x', 'bbox_y', 'bbox_w', 'bbox_h',
                'timestamp', 'segment_index',
                'frame_filename'
            ])
            writer.writeheader()
            
            for det in detections:
                # Copy frame
                if det.frame_path_annotated:
                    src = Path(det.frame_path_annotated)
                    dst = export_path / f"det_{det.id}.jpg"
                    shutil.copy(src, dst)
                    
                    writer.writerow({
                        'id': det.id,
                        'object_type': det.object_type,
                        'confidence': det.confidence,
                        'bbox_x': det.bbox_x,
                        'bbox_y': det.bbox_y,
                        'bbox_w': det.bbox_w,
                        'bbox_h': det.bbox_h,
                        'timestamp': det.unix_timestamp,
                        'segment_index': det.segment_index,
                        'frame_filename': f"det_{det.id}.jpg"
                    })
        
        LOG_INFO(f"Exported {len(detections)} detections to {export_path}")
```

---

## Phase 3: HTTP API for Frame Serving

### 3.1 New HTTP Endpoints

```
GET /detections/search?start_ts=X&end_ts=Y
  Returns: JSON with detections + frame URLs

GET /detections/frame/<detection_id>
  Returns: JPEG image (annotated frame)

GET /detections/raw-frame/<detection_id>
  Returns: JPEG image (raw frame, if saved)

GET /detections/stats?stream=camera-1&hour=2026-08-10_12
  Returns: JSON with hourly stats
```

### 3.2 Integration with Existing HTTP Server

```cpp
// In src/server/HttpServer.cpp, add detection endpoints:

if (request_path.find("/detections/") == 0) {
    // Route to Python detection API
    std::string response = call_python("search_api.http_handler", request_path, params);
    send_response(200, "application/json", response);
}

if (request_path.find("/detections/frame/") == 0) {
    // Serve JPEG directly
    int det_id = extract_id_from_path(request_path);
    std::vector<uint8_t> frame_data = call_python("search_api.serve_frame", det_id);
    send_response(200, "image/jpeg", frame_data);
}
```

---

## Phase 4: Web UI Integration

### 4.1 Detection Gallery (`web/detections.html`)

```html
<!DOCTYPE html>
<html>
<head>
    <title>Detection Gallery</title>
    <style>
        .detection-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
            gap: 20px;
        }
        .detection-card {
            border: 1px solid #ddd;
            border-radius: 8px;
            overflow: hidden;
        }
        .detection-card img {
            width: 100%;
            height: auto;
        }
        .detection-info {
            padding: 10px;
            font-size: 14px;
        }
        .confidence {
            color: #666;
        }
    </style>
</head>
<body>
    <div class="detection-grid" id="gallery"></div>
    
    <script>
        // Fetch detections from past 24 hours
        const end_ts = Math.floor(Date.now() / 1000);
        const start_ts = end_ts - 86400;
        
        fetch(`/detections/search?start_ts=${start_ts}&end_ts=${end_ts}`)
            .then(r => r.json())
            .then(detections => {
                const gallery = document.getElementById('gallery');
                detections.forEach(det => {
                    const card = document.createElement('div');
                    card.className = 'detection-card';
                    card.innerHTML = `
                        <img src="${det.frame_url}" />
                        <div class="detection-info">
                            <strong>${det.object_type}</strong><br/>
                            <span class="confidence">Confidence: ${(det.confidence * 100).toFixed(1)}%</span><br/>
                            <span>${new Date(det.unix_timestamp * 1000).toLocaleString()}</span>
                        </div>
                    `;
                    gallery.appendChild(card);
                });
            });
    </script>
</body>
</html>
```

---

## Configuration (Revised)

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
  
  # I/O
  hls_input_dir: /app/hls_output/{stream_id}
  database_path: /app/detections.db
  detections_output_dir: /app/detections            # NEW
  save_raw_frames: false                            # NEW
  
  # Performance
  max_workers: 4
  batch_size: 1
  device: cpu
  
  # Cleanup
  retention_days: 30                                # NEW: 30 days for frames
  
  # Annotation
  annotate_thickness: 2
  annotate_font_scale: 0.6
  annotation_quality: 85                            # JPEG quality 0-100
```

---

## Implementation Timeline (Revised)

| Phase | Component | Time Est. | Status |
|-------|-----------|-----------|--------|
| 1 | Infra + DB + FrameAnnotator | 1.5 hrs | Start here |
| 2 | DetectionWorker (save frames) | 1 hr | Sequential |
| 3 | HTTP API + frame serving | 1 hr | After Phase 2 |
| 4 | Web UI (gallery) | 1 hr | Final |
| 5 | Testing + benchmarking | 1 hr | All phases |
| **Total** | | **5-6 hours** | |

---

## Storage Considerations

### Disk Usage Estimate

**Per Detection:**
- Annotated JPEG (640×480, quality 85): ~30KB
- Raw JPEG (quality 90): ~35KB

**Daily Volume:**
```
Scenario: 1 camera, 5 people/hour + 3 cars/hour = 8 detections/hour
Daily: 8 × 24 = 192 detections/day
  Annotated: 192 × 30KB = 5.8 MB/day
  Total (30-day retention): ~174 MB

Scenario: 10 cameras, same rate
Daily: 1,920 detections/day
  Total (30-day retention): ~1.7 GB
```

### Cleanup Strategy

```python
# Weekly cleanup (runs in background)
def cleanup_old_frames():
    retention_seconds = config.retention_days * 86400
    now = time.time()
    
    for frame_file in Path(detections_output_dir).rglob("*.jpg"):
        age_seconds = now - frame_file.stat().st_mtime
        if age_seconds > retention_seconds:
            frame_file.unlink()
            
            # Remove corresponding DB entry
            det_id = extract_id_from_filename(frame_file.name)
            db.delete_detection(det_id)
```

---

## Metacognitive Summary: Why This Design Is Correct

### 1. **Separate Concerns**
- ✅ Detection logic (YOLOv8) independent from visualization (annotator)
- ✅ Storage (database) independent from serving (HTTP API)
- ✅ Easy to swap annotator, frame format, or storage backend

### 2. **Scalability**
- ✅ Database remains lean (refs only, not image data)
- ✅ Frames served via standard HTTP (cacheable, CDN-friendly)
- ✅ Frame cleanup separate from detection lifecycle
- ✅ Can scale HTTP serving independently (add reverse proxy, CDN)

### 3. **Inspection & Verification**
- ✅ Each detection has associated frame
- ✅ Operator can verify false positives visually
- ✅ Confidence score + visual review = trust
- ✅ Training data collection enabled (export with frames)

### 4. **Interview Assignment Context**
- ✅ Shows actual detected objects (frames with bboxes)
- ✅ Demonstrates accuracy (can see false positives)
- ✅ Provides search capability (find all people in timestamp range)
- ✅ Professional presentation (gallery UI)

### 5. **Deployment Simplicity**
- ✅ No special database setup (SQLite native)
- ✅ No image service needed (HTTP API handles it)
- ✅ Standard cleanup patterns (delete old files + DB records)
- ✅ Works offline (files on disk, not cloud)

---

## Success Criteria (Revised)

✅ YOLOv8 detects person & car, saves annotated frame  
✅ Frame paths stored in database  
✅ Frames served via HTTP GET `/detections/frame/<id>`  
✅ Detection gallery shows thumbnails + confidence  
✅ Search API returns detections with frame URLs  
✅ Old frames auto-deleted after 30 days  
✅ Disk usage < 2GB for typical deployment  
✅ CPU overhead < 5%  
✅ Memory < 300MB  
✅ Comprehensive logging of frame operations  

---

## Ready to Implement Phase 1?

Should I begin with:
1. Create database schema with `frame_path_annotated`, `frame_path_raw` columns
2. Implement `FrameAnnotator` class with bbox drawing
3. Implement `DetectionWorker` frame saving logic
4. Update `DetectionDatabase` to store frame paths

This gives us the complete pipeline from detection → annotation → storage.
