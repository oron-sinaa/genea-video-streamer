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
            frame: Input image (BGR, HxWx3)
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
