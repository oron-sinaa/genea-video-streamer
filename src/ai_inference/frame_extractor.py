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
