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
            frame: Input image (BGR, HxWx3)
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
