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
