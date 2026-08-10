# AI Inference Implementation - Step-by-Step Specifics

## Phase 1: Infrastructure & Foundation (Tasks 1-7)

---

### TASK 1: Create Python Package Structure
**Deliverable**: Directory structure + `__init__.py`

```bash
# Commands to execute:
mkdir -p src/ai_inference
touch src/ai_inference/__init__.py
```

**File: `src/ai_inference/__init__.py`**
```python
"""AI Inference module for object detection in video streams."""

__version__ = "0.1.0"
__all__ = [
    'config',
    'database',
    'detector',
    'frame_annotator',
    'frame_extractor',
    'hls_reader',
    'detection_worker',
    'search_api',
]
```

**Verification**: 
```bash
python3 -c "import sys; sys.path.insert(0, 'src'); import ai_inference; print('✓ Package loads')"
```

---

### TASK 2: Create `config.py` - Configuration Loader
**File: `src/ai_inference/config.py`**

```python
"""Load and validate AI inference configuration from YAML."""

import yaml
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

@dataclass
class AiInferenceConfig:
    """AI Inference configuration."""
    
    # Core settings
    enabled: bool = True
    model: str = "yolov8n"
    classes: List[str] = None  # Default: ['person', 'car']
    confidence_threshold: float = 0.5
    
    # Inference
    inference_interval_s: int = 5
    frames_per_segment: int = 1
    
    # I/O
    hls_input_dir: str = "/app/hls_output/{stream_id}"
    database_path: str = "/app/detections.db"
    detections_output_dir: str = "/app/detections"
    save_raw_frames: bool = False
    
    # Performance
    max_workers: int = 4
    batch_size: int = 1
    device: str = "cpu"  # 'cpu' or 'cuda'
    
    # Cleanup
    retention_days: int = 30
    
    # Annotation
    annotate_thickness: int = 2
    annotate_font_scale: float = 0.6
    annotation_quality: int = 85  # JPEG 0-100
    
    # Logging
    log_level: str = "INFO"
    log_detections: bool = True
    
    # Stream info
    stream_id: str = "camera-1"
    
    def __post_init__(self):
        """Validate configuration after initialization."""
        if self.classes is None:
            self.classes = ['person', 'car']
        
        if self.confidence_threshold < 0 or self.confidence_threshold > 1:
            raise ValueError("confidence_threshold must be 0-1")
        
        if self.retention_days < 1:
            raise ValueError("retention_days must be >= 1")
        
        # Expand template variables in paths
        self.hls_input_dir = self.hls_input_dir.replace('{stream_id}', self.stream_id)


def load_config(config_path: str) -> AiInferenceConfig:
    """Load configuration from YAML file."""
    config_file = Path(config_path)
    
    if not config_file.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")
    
    with open(config_file, 'r') as f:
        data = yaml.safe_load(f)
    
    ai_config_data = data.get('ai_inference', {})
    return AiInferenceConfig(**ai_config_data)


def get_default_config() -> AiInferenceConfig:
    """Return default configuration."""
    return AiInferenceConfig()
```

**Verification**:
```bash
python3 -c "
import sys
sys.path.insert(0, 'src')
from ai_inference.config import get_default_config
cfg = get_default_config()
assert cfg.model == 'yolov8n'
assert cfg.confidence_threshold == 0.5
print('✓ Config loads with defaults')
"
```

---

### TASK 3: Create `ai_inference_config.yaml` Template
**File: `config/ai_inference_config.yaml`**

```yaml
# AI Inference Configuration

ai_inference:
  # Model Settings
  enabled: true
  model: yolov8n                    # 'yolov8n' (nano), 'yolov8s' (small), etc.
  classes: [person, car]            # Classes to detect
  confidence_threshold: 0.5          # Detection confidence threshold (0-1)
  
  # Inference Settings
  inference_interval_s: 5            # Process segment every N seconds
  frames_per_segment: 1              # Frames to extract per 2-second segment
  
  # I/O Paths
  hls_input_dir: /app/hls_output/{stream_id}   # {stream_id} replaced at runtime
  database_path: /app/detections.db
  detections_output_dir: /app/detections
  save_raw_frames: false             # Also save raw frames (before annotation)
  
  # Performance
  max_workers: 4                     # CPU inference parallelism
  batch_size: 1                      # Frames per batch
  device: cpu                        # 'cpu' (no GPU required)
  
  # Cleanup & Retention
  retention_days: 30                 # Keep detection frames N days
  
  # Annotation Style
  annotate_thickness: 2              # Bbox line thickness
  annotate_font_scale: 0.6           # Label text size
  annotation_quality: 85             # JPEG quality (0-100)
  
  # Logging
  log_level: INFO                    # DEBUG, INFO, WARN, ERROR
  log_detections: true               # Log each detection to console
  
  # Stream Identification
  stream_id: camera-1                # Unique stream ID (used in paths/DB)
```

---

### TASK 4: Create `database.py` - SQLite Wrapper
**File: `src/ai_inference/database.py`**

**Part A: Schema Creation** (first 100 lines)

```python
"""SQLite database for detection storage and queries."""

import sqlite3
import time
from datetime import datetime
from pathlib import Path
from typing import List, Optional, Dict, Any

# Database schema (SQL)
SCHEMA = """
CREATE TABLE IF NOT EXISTS detections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    object_type TEXT NOT NULL,
    confidence REAL NOT NULL,
    bbox_x REAL NOT NULL,
    bbox_y REAL NOT NULL,
    bbox_w REAL NOT NULL,
    bbox_h REAL NOT NULL,
    unix_timestamp INTEGER NOT NULL,
    human_readable_time TEXT,
    segment_index INTEGER,
    segment_filename TEXT,
    frame_number_in_segment INTEGER,
    frame_path_annotated TEXT,
    frame_path_raw TEXT,
    stream_id TEXT,
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);

CREATE TABLE IF NOT EXISTS detection_stats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stream_id TEXT,
    hour_bucket TEXT,
    person_count INTEGER DEFAULT 0,
    car_count INTEGER DEFAULT 0,
    total_detections INTEGER DEFAULT 0,
    avg_confidence REAL,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    UNIQUE(stream_id, hour_bucket)
);

CREATE INDEX IF NOT EXISTS idx_detections_object_type ON detections(object_type);
CREATE INDEX IF NOT EXISTS idx_detections_timestamp ON detections(unix_timestamp);
CREATE INDEX IF NOT EXISTS idx_detections_confidence ON detections(confidence);
CREATE INDEX IF NOT EXISTS idx_detections_segment ON detections(segment_index);
CREATE INDEX IF NOT EXISTS idx_stats_hour ON detection_stats(hour_bucket);
"""
```

**Part B: DetectionDatabase Class** (remaining code)

```python
class DetectionDatabase:
    """Database wrapper for detection storage and queries."""
    
    def __init__(self, db_path: str):
        """Initialize database connection and create schema."""
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        
        self.conn = sqlite3.connect(str(self.db_path), check_same_thread=False)
        self.conn.execute("PRAGMA journal_mode=WAL")  # Enable WAL for concurrency
        
        # Create schema
        for statement in SCHEMA.split(';'):
            if statement.strip():
                self.conn.execute(statement)
        self.conn.commit()
    
    def insert_detection(self, detection: Dict[str, Any]) -> int:
        """Insert detection, return row ID."""
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
            detection['object_type'],
            detection['confidence'],
            detection['bbox_x'],
            detection['bbox_y'],
            detection['bbox_w'],
            detection['bbox_h'],
            detection['unix_timestamp'],
            datetime.fromtimestamp(detection['unix_timestamp']).strftime('%Y-%m-%d %H:%M:%S'),
            detection.get('segment_index'),
            detection.get('segment_filename'),
            detection.get('frame_number_in_segment'),
            detection.get('frame_path_annotated'),
            detection.get('frame_path_raw'),
            detection['stream_id']
        ))
        self.conn.commit()
        return cursor.lastrowid
    
    def get_detections_by_time(self, start_ts: int, end_ts: int) -> List[Dict]:
        """Query detections by timestamp range."""
        cursor = self.conn.cursor()
        cursor.execute("""
            SELECT * FROM detections
            WHERE unix_timestamp BETWEEN ? AND ?
            ORDER BY unix_timestamp DESC
        """, (start_ts, end_ts))
        
        rows = cursor.fetchall()
        columns = [desc[0] for desc in cursor.description]
        return [dict(zip(columns, row)) for row in rows]
    
    def get_detections_by_object_type(self, obj_type: str, limit: int = 100) -> List[Dict]:
        """Query detections by class."""
        cursor = self.conn.cursor()
        cursor.execute("""
            SELECT * FROM detections
            WHERE object_type = ?
            ORDER BY unix_timestamp DESC
            LIMIT ?
        """, (obj_type, limit))
        
        rows = cursor.fetchall()
        columns = [desc[0] for desc in cursor.description]
        return [dict(zip(columns, row)) for row in rows]
    
    def get_detection_by_id(self, det_id: int) -> Optional[Dict]:
        """Get single detection by ID."""
        cursor = self.conn.cursor()
        cursor.execute("SELECT * FROM detections WHERE id = ?", (det_id,))
        row = cursor.fetchone()
        
        if row:
            columns = [desc[0] for desc in cursor.description]
            return dict(zip(columns, row))
        return None
    
    def delete_detection(self, det_id: int) -> bool:
        """Delete detection by ID."""
        cursor = self.conn.cursor()
        cursor.execute("DELETE FROM detections WHERE id = ?", (det_id,))
        self.conn.commit()
        return cursor.rowcount > 0
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get overall statistics."""
        cursor = self.conn.cursor()
        
        cursor.execute("SELECT COUNT(*) FROM detections")
        total = cursor.fetchone()[0]
        
        cursor.execute("SELECT object_type, COUNT(*) FROM detections GROUP BY object_type")
        by_type = dict(cursor.fetchall())
        
        cursor.execute("SELECT AVG(confidence) FROM detections")
        avg_conf = cursor.fetchone()[0] or 0.0
        
        return {
            'total_detections': total,
            'by_type': by_type,
            'average_confidence': avg_conf
        }
    
    def cleanup_old_records(self, retention_days: int) -> int:
        """Delete detection records older than N days."""
        retention_seconds = retention_days * 86400
        cutoff_ts = int(time.time()) - retention_seconds
        
        cursor = self.conn.cursor()
        cursor.execute("DELETE FROM detections WHERE unix_timestamp < ?", (cutoff_ts,))
        self.conn.commit()
        
        return cursor.rowcount
    
    def close(self):
        """Close database connection."""
        self.conn.close()
```

**Verification**:
```bash
python3 << 'EOF'
import sys, tempfile
sys.path.insert(0, 'src')
from ai_inference.database import DetectionDatabase

with tempfile.NamedTemporaryFile(suffix='.db') as f:
    db = DetectionDatabase(f.name)
    
    # Insert test detection
    det = {
        'object_type': 'person',
        'confidence': 0.95,
        'bbox_x': 0.1, 'bbox_y': 0.2, 'bbox_w': 0.3, 'bbox_h': 0.4,
        'unix_timestamp': int(__import__('time').time()),
        'stream_id': 'test'
    }
    det_id = db.insert_detection(det)
    
    # Query back
    result = db.get_detection_by_id(det_id)
    assert result['object_type'] == 'person'
    print('✓ Database insert/query works')
    
    db.close()
EOF
```

---

### TASK 5: Create Detection Dataclasses
**File: `src/ai_inference/detection.py`**

```python
"""Data classes for detection results."""

from dataclasses import dataclass, field
from typing import Optional
import time

@dataclass
class Detection:
    """Single object detection result."""
    
    object_type: str                  # 'person' or 'car'
    confidence: float                 # 0.0-1.0
    bbox_x: float                     # Top-left X (0-1 normalized)
    bbox_y: float                     # Top-left Y (0-1 normalized)
    bbox_w: float                     # Width (0-1 normalized)
    bbox_h: float                     # Height (0-1 normalized)
    
    unix_timestamp: int = field(default_factory=lambda: int(time.time()))
    segment_index: Optional[int] = None
    segment_filename: Optional[str] = None
    frame_number_in_segment: Optional[int] = None
    
    frame_path_annotated: Optional[str] = None
    frame_path_raw: Optional[str] = None
    
    stream_id: str = "camera-1"
    
    def to_dict(self) -> dict:
        """Convert to dictionary for database storage."""
        return {
            'object_type': self.object_type,
            'confidence': self.confidence,
            'bbox_x': self.bbox_x,
            'bbox_y': self.bbox_y,
            'bbox_w': self.bbox_w,
            'bbox_h': self.bbox_h,
            'unix_timestamp': self.unix_timestamp,
            'segment_index': self.segment_index,
            'segment_filename': self.segment_filename,
            'frame_number_in_segment': self.frame_number_in_segment,
            'frame_path_annotated': self.frame_path_annotated,
            'frame_path_raw': self.frame_path_raw,
            'stream_id': self.stream_id,
        }


@dataclass
class SegmentInfo:
    """Information about HLS segment."""
    
    filename: str                      # 'segment_000042.ts'
    index: int                         # 42
    filepath: str                      # '/app/hls_output/camera-1/segment_000042.ts'
    duration: float = 2.0              # Seconds
    discontinuity: bool = False        # Marks restart boundary
```

**Verification**:
```bash
python3 -c "
import sys
sys.path.insert(0, 'src')
from ai_inference.detection import Detection
d = Detection(object_type='person', confidence=0.85, bbox_x=0.1, bbox_y=0.2, bbox_w=0.3, bbox_h=0.4)
assert d.object_type == 'person'
print('✓ Detection dataclass works')
"
```

---

### TASK 6: Create `frame_annotator.py` - Bbox Drawing
**File: `src/ai_inference/frame_annotator.py`**

```python
"""Frame annotation with bounding boxes."""

import cv2
import numpy as np
from pathlib import Path
from typing import List
from ai_inference.detection import Detection


class FrameAnnotator:
    """Annotate frames with bounding boxes."""
    
    def __init__(self, thickness: int = 2, font_scale: float = 0.6):
        """Initialize annotator."""
        self.thickness = thickness
        self.font_scale = font_scale
        self.colors = {
            'person': (0, 255, 0),     # Green (BGR)
            'car': (255, 0, 0),        # Blue (BGR)
        }
    
    def annotate_frame(
        self,
        frame: np.ndarray,
        detections: List[Detection],
        draw_confidence: bool = True
    ) -> np.ndarray:
        """
        Draw bounding boxes on frame.
        
        Args:
            frame: Input image (BGR, H×W×3)
            detections: List of Detection objects
            draw_confidence: Show confidence % in label
            
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
            
            # Clamp to image bounds
            x1 = max(0, x1)
            y1 = max(0, y1)
            x2 = min(width, x2)
            y2 = min(height, y2)
            
            # Get color for class
            color = self.colors.get(det.object_type, (255, 255, 255))
            
            # Draw bounding box
            cv2.rectangle(output, (x1, y1), (x2, y2), color, self.thickness)
            
            # Build label
            label = det.object_type.upper()
            if draw_confidence:
                label += f" {det.confidence*100:.0f}%"
            
            # Measure text size
            font = cv2.FONT_HERSHEY_SIMPLEX
            text_size = cv2.getTextSize(label, font, self.font_scale, self.thickness)[0]
            
            # Background rectangle for text
            text_x = x1
            text_y = max(y1 - 5, text_size[1] + 5)
            bg_x1 = text_x
            bg_y1 = text_y - text_size[1] - 5
            bg_x2 = text_x + text_size[0] + 5
            bg_y2 = text_y + 5
            
            cv2.rectangle(output, (bg_x1, bg_y1), (bg_x2, bg_y2), color, -1)
            
            # Draw text
            cv2.putText(output, label, (text_x + 2, text_y - 3),
                       font, self.font_scale, (0, 0, 0), self.thickness)
        
        return output
    
    def save_frame(self, frame: np.ndarray, path: str, quality: int = 85):
        """
        Save frame as JPEG.
        
        Args:
            frame: Image to save (BGR)
            path: Output file path
            quality: JPEG quality (0-100)
        """
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(path, frame, [cv2.IMWRITE_JPEG_QUALITY, quality])
```

**Verification**:
```bash
python3 << 'EOF'
import sys, tempfile, numpy as np
sys.path.insert(0, 'src')
from ai_inference.frame_annotator import FrameAnnotator
from ai_inference.detection import Detection

annotator = FrameAnnotator()
frame = np.zeros((480, 640, 3), dtype=np.uint8)

det = Detection(
    object_type='person',
    confidence=0.95,
    bbox_x=0.1, bbox_y=0.2, bbox_w=0.3, bbox_h=0.4
)

annotated = annotator.annotate_frame(frame, [det])
assert annotated.shape == frame.shape
print('✓ FrameAnnotator works')

with tempfile.NamedTemporaryFile(suffix='.jpg') as f:
    annotator.save_frame(annotated, f.name)
    print('✓ Frame save works')
EOF
```

---

## Phase 2: Segment & Detection Modules (Tasks 8-11)

### TASK 7: Create `frame_extractor.py`
**File: `src/ai_inference/frame_extractor.py`**

```python
"""Extract frames from MPEG-TS (.ts) segments."""

import cv2
import numpy as np
from typing import List, Optional
from pathlib import Path


class FrameExtractor:
    """Extract frames from .ts video files."""
    
    def __init__(self):
        """Initialize frame extractor."""
        pass
    
    def extract_frame_at_time(self, ts_file: str, time_s: float) -> Optional[np.ndarray]:
        """
        Extract frame from .ts file at specific timestamp.
        
        Args:
            ts_file: Path to .ts file
            time_s: Timestamp in seconds (0 = start)
            
        Returns:
            Frame as BGR numpy array (H×W×3), or None if failed
        """
        if not Path(ts_file).exists():
            raise FileNotFoundError(f"Segment file not found: {ts_file}")
        
        cap = cv2.VideoCapture(ts_file)
        if not cap.isOpened():
            return None
        
        try:
            # Get video properties
            fps = cap.get(cv2.CAP_PROP_FPS)
            if fps <= 0:
                fps = 30  # Fallback
            
            # Calculate frame number
            frame_num = int(time_s * fps)
            
            # Seek to frame
            cap.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
            
            # Read frame
            ret, frame = cap.read()
            if not ret:
                return None
            
            return frame
        
        finally:
            cap.release()
    
    def extract_frames_interval(self, ts_file: str, interval_s: float) -> List[np.ndarray]:
        """
        Extract frames at regular intervals from segment.
        
        Args:
            ts_file: Path to .ts file
            interval_s: Extract frame every N seconds
            
        Returns:
            List of frames
        """
        if not Path(ts_file).exists():
            raise FileNotFoundError(f"Segment file not found: {ts_file}")
        
        frames = []
        cap = cv2.VideoCapture(ts_file)
        
        if not cap.isOpened():
            return frames
        
        try:
            fps = cap.get(cv2.CAP_PROP_FPS)
            total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            
            if fps <= 0:
                fps = 30
            
            frame_interval = int(interval_s * fps)
            
            # Extract frames
            frame_num = 0
            while True:
                cap.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
                ret, frame = cap.read()
                
                if not ret:
                    break
                
                frames.append(frame)
                frame_num += frame_interval
        
        finally:
            cap.release()
        
        return frames
    
    def get_segment_duration(self, ts_file: str) -> float:
        """Get segment duration in seconds."""
        if not Path(ts_file).exists():
            return 0.0
        
        cap = cv2.VideoCapture(ts_file)
        if not cap.isOpened():
            return 0.0
        
        try:
            fps = cap.get(cv2.CAP_PROP_FPS)
            total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            
            if fps <= 0:
                fps = 30
            
            return total_frames / fps
        
        finally:
            cap.release()
```

---

### TASK 8: Create `detector.py` - YOLOv8 Wrapper
**File: `src/ai_inference/detector.py`**

```python
"""YOLOv8 object detector wrapper."""

from typing import List, Dict, Optional
from ultralytics import YOLO
import numpy as np
from ai_inference.detection import Detection


class YoloDetector:
    """Wrapper around YOLOv8 model."""
    
    # Map COCO class IDs to our target classes
    COCO_CLASSES = {
        0: 'person',
        2: 'car',
        5: 'car',     # bus (treat as car)
        7: 'car',     # truck (treat as car)
    }
    
    def __init__(self, model_size: str = 'n', classes: List[str] = None, device: str = 'cpu'):
        """
        Initialize YOLOv8 detector.
        
        Args:
            model_size: 'n' (nano), 's' (small), 'm' (medium), 'l' (large)
            classes: List of target classes ['person', 'car']
            device: 'cpu' or 'cuda'
        """
        self.model_size = model_size
        self.target_classes = set(classes or ['person', 'car'])
        self.device = device
        
        # Load model (will auto-download if missing)
        model_name = f'yolov8{model_size}.pt'
        self.model = YOLO(model_name)
        self.model.to(device)
    
    def detect(self, frame: np.ndarray, confidence_threshold: float = 0.5) -> List[Detection]:
        """
        Run inference on frame.
        
        Args:
            frame: Input image (BGR, H×W×3)
            confidence_threshold: Filter detections below this confidence
            
        Returns:
            List of Detection objects
        """
        # Run inference
        results = self.model(frame, verbose=False)
        
        detections = []
        if results and len(results) > 0:
            result = results[0]
            
            # Extract detections
            for box in result.boxes:
                # Get class ID and name
                class_id = int(box.cls[0])
                class_name = result.names.get(class_id, '')
                
                # Map to our target classes
                if class_id not in self.COCO_CLASSES:
                    continue
                
                mapped_class = self.COCO_CLASSES[class_id]
                if mapped_class not in self.target_classes:
                    continue
                
                # Get confidence
                confidence = float(box.conf[0])
                if confidence < confidence_threshold:
                    continue
                
                # Get bbox (already normalized 0-1 by YOLO)
                x1, y1, x2, y2 = box.xyxyn[0].tolist()  # Normalized coords
                
                bbox_x = float(x1)
                bbox_y = float(y1)
                bbox_w = float(x2 - x1)
                bbox_h = float(y2 - y1)
                
                # Create Detection object
                detection = Detection(
                    object_type=mapped_class,
                    confidence=confidence,
                    bbox_x=bbox_x,
                    bbox_y=bbox_y,
                    bbox_w=bbox_w,
                    bbox_h=bbox_h,
                )
                
                detections.append(detection)
        
        return detections
    
    def benchmark(self, num_iterations: int = 100) -> Dict[str, float]:
        """
        Benchmark model performance.
        
        Returns:
            Dict with fps, memory_mb, avg_latency_ms
        """
        import time
        
        # Create dummy frame
        frame = np.zeros((640, 480, 3), dtype=np.uint8)
        
        start_time = time.time()
        for _ in range(num_iterations):
            _ = self.detect(frame)
        
        elapsed = time.time() - start_time
        fps = num_iterations / elapsed
        
        return {
            'fps': fps,
            'avg_latency_ms': (elapsed / num_iterations) * 1000,
        }
```

---

### TASK 9: Create `hls_reader.py` - M3U8 Parser
**File: `src/ai_inference/hls_reader.py`**

```python
"""Parse HLS manifests and track segment availability."""

from pathlib import Path
from typing import List, Optional
import time
import re
from ai_inference.detection import SegmentInfo


class HlsReader:
    """Read and parse HLS manifest files."""
    
    def __init__(self, hls_dir: str):
        """Initialize reader for HLS directory."""
        self.hls_dir = Path(hls_dir)
        self.last_segment_index = -1
    
    def read_manifest(self, manifest_file: str = 'archive.m3u8') -> List[SegmentInfo]:
        """
        Parse .m3u8 manifest file.
        
        Args:
            manifest_file: 'archive.m3u8' or 'live.m3u8'
            
        Returns:
            List of SegmentInfo objects
        """
        manifest_path = self.hls_dir / manifest_file
        
        if not manifest_path.exists():
            return []
        
        segments = []
        discontinuity = False
        
        with open(manifest_path, 'r') as f:
            lines = f.readlines()
        
        i = 0
        while i < len(lines):
            line = lines[i].strip()
            
            # Check for discontinuity marker
            if line == '#EXT-X-DISCONTINUITY':
                discontinuity = True
                i += 1
                continue
            
            # Parse EXTINF line
            if line.startswith('#EXTINF:'):
                # Format: #EXTINF:2.0,
                match = re.match(r'#EXTINF:([\d.]+)', line)
                duration = float(match.group(1)) if match else 2.0
                
                # Next line should be segment filename
                i += 1
                if i < len(lines):
                    filename = lines[i].strip()
                    
                    # Extract segment index
                    seg_match = re.match(r'segment_(\d+)\.ts', filename)
                    if seg_match:
                        index = int(seg_match.group(1))
                        filepath = str(self.hls_dir / filename)
                        
                        seg = SegmentInfo(
                            filename=filename,
                            index=index,
                            filepath=filepath,
                            duration=duration,
                            discontinuity=discontinuity
                        )
                        segments.append(seg)
                        discontinuity = False
            
            i += 1
        
        return segments
    
    def get_latest_segment(self) -> Optional[SegmentInfo]:
        """Get most recently created segment."""
        manifest_path = self.hls_dir / 'archive.m3u8'
        
        if not manifest_path.exists():
            return None
        
        segments = self.read_manifest('archive.m3u8')
        if segments:
            return segments[-1]  # Last segment in list
        
        return None
    
    def get_segment_by_index(self, index: int) -> Optional[SegmentInfo]:
        """Get segment by index number."""
        filename = f"segment_{index:06d}.ts"
        filepath = self.hls_dir / filename
        
        if filepath.exists():
            return SegmentInfo(
                filename=filename,
                index=index,
                filepath=str(filepath),
                duration=2.0,
            )
        
        return None
    
    def wait_for_new_segment(self, timeout_s: int = 30) -> Optional[SegmentInfo]:
        """
        Block until new segment appears.
        
        Args:
            timeout_s: Max seconds to wait
            
        Returns:
            New SegmentInfo, or None if timeout
        """
        start_time = time.time()
        
        while True:
            latest = self.get_latest_segment()
            
            if latest and latest.index > self.last_segment_index:
                self.last_segment_index = latest.index
                return latest
            
            # Check timeout
            if time.time() - start_time > timeout_s:
                return None
            
            # Sleep briefly before re-checking
            time.sleep(0.5)
```

**Verification**: (After tasks 9-11 complete, full end-to-end test)

---

## Phase 3: Integration & Testing (Tasks 12-25)

### TASK 10-11: Detection Worker & Search API
*(Follow same pattern as tasks above, create in separate step)*

### TASK 12: Create Detections Directory & .gitignore
```bash
mkdir -p detections/camera-1/annotated
mkdir -p detections/camera-1/raw
touch detections/.gitignore
```

**File: `detections/.gitignore`**
```
*.jpg
*.jpeg
*.png
*.db
*.tmp
```

### TASK 13: Update requirements.txt
```bash
# Add to requirements.txt:
cat >> requirements.txt << 'EOF'

# AI Inference
ultralytics>=8.0.0
opencv-python>=4.8.0
numpy>=1.24.0
EOF

pip install -r requirements.txt
```

---

## Verification Commands (Tasks 17-23)

**TASK 17 - Database**:
```bash
python3 src/ai_inference/test_database.py
```

**TASK 18 - FrameAnnotator**:
```bash
python3 src/ai_inference/test_frame_annotator.py
```

**TASK 19 - YoloDetector**:
```bash
python3 << 'EOF'
import sys
sys.path.insert(0, 'src')
from ai_inference.detector import YoloDetector

print("Loading YOLOv8 Nano...")
detector = YoloDetector(model_size='n', device='cpu')
print("✓ Model loaded")

print("Running benchmark...")
stats = detector.benchmark(num_iterations=10)
print(f"✓ FPS: {stats['fps']:.1f}, Latency: {stats['avg_latency_ms']:.1f}ms")
EOF
```

---

**Total Implementation Time Estimate:**
- Phase 1 (Infrastructure): 2-3 hours
- Phase 2 (Core Modules): 2-3 hours  
- Phase 3 (Integration): 1-2 hours
- Testing & Debugging: 1-2 hours
- **Total: 6-10 hours**

