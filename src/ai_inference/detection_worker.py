"""Main detection worker thread."""

import threading
import time
import logging
from pathlib import Path
from typing import Optional
import numpy as np

from ai_inference.config import AiInferenceConfig, StreamConfig
from ai_inference.database import DetectionDatabase
from ai_inference.detector import YoloDetector
from ai_inference.frame_extractor import FrameExtractor
from ai_inference.frame_annotator import FrameAnnotator
from ai_inference.hls_reader import HlsReader
from ai_inference.detection import Detection


logger = logging.getLogger(__name__)


class DetectionWorker:
    """
    Detection worker for a single stream.
    
    Processes HLS segments and runs object detection.
    Each worker handles exactly one stream with independent paths and database writes.
    """
    
    def __init__(self, config: AiInferenceConfig, stream_config: StreamConfig):
        """
        Initialize worker for a specific stream.
        
        Args:
            config: Global inference configuration (model, device, etc.)
            stream_config: Stream-specific configuration (paths, stream_id)
        """
        self.config = config
        self.stream_config = stream_config
        self.running = False
        self.thread = None
        
        # Extract stream-specific info
        self.stream_id = stream_config.stream_id
        self.hls_input_dir = stream_config.hls_input_dir
        self.detections_output_dir = stream_config.detections_output_dir
        
        # Initialize components
        self.db = DetectionDatabase(config.database_path)
        self.detector = YoloDetector(
            model_size=config.model.replace('yolov8', ''),
            classes=config.classes,
            device=config.device
        )
        self.frame_extractor = FrameExtractor()
        self.frame_annotator = FrameAnnotator(
            thickness=config.annotate_thickness,
            font_scale=config.annotate_font_scale
        )
        self.hls_reader = HlsReader(self.hls_input_dir)
        
        # Create stream-specific output directories
        Path(self.detections_output_dir).mkdir(parents=True, exist_ok=True)
        annotated_dir = Path(self.detections_output_dir) / "annotated"
        annotated_dir.mkdir(parents=True, exist_ok=True)
        
        raw_dir = Path(self.detections_output_dir) / "raw"
        raw_dir.mkdir(parents=True, exist_ok=True)
    
    def run(self):
        """Main inference loop (runs in thread)."""
        self.running = True
        logger.info(
            f"DetectionWorker[{self.stream_id}] started: "
            f"model={self.config.model}, interval={self.config.inference_interval_s}s, "
            f"input={self.hls_input_dir}"
        )
        
        try:
            while self.running:
                # Wait for new segment
                segment = self.hls_reader.wait_for_new_segment(timeout_s=10)
                
                if segment is None:
                    continue
                
                if not Path(segment.filepath).exists():
                    logger.debug(f"Segment file disappeared: {segment.filename}")
                    continue
                
                try:
                    # Extract frames at interval
                    self._process_segment(segment)
                
                except Exception as e:
                    logger.error(f"Error processing segment {segment.filename}: {e}")
                    continue
        
        except Exception as e:
            logger.error(f"DetectionWorker crash: {e}")
        
        finally:
            self.db.close()
            logger.info("DetectionWorker stopped")
    
    def _process_segment(self, segment):
        """Process one segment file."""
        logger.debug(f"Processing segment {segment.filename}")
        
        # Extract frame from start of segment (frame 0)
        # OpenCV seeks poorly in MPEG-TS, so use first frame instead
        time_s = 0.0
        
        frame = self.frame_extractor.extract_frame_at_time(segment.filepath, time_s)
        if frame is None:
            logger.warning(f"Failed to extract frame from {segment.filename}")
            return
        
        try:
            # Run inference
            detections = self.detector.detect(frame, self.config.confidence_threshold)
            
            if self.config.log_detections and detections:
                logger.info(f"Segment {segment.index}: {len(detections)} detections")
            
            # Save and store detections
            for det in detections:
                # Annotate and save frame
                annotated_frame = self.frame_annotator.annotate_frame(frame, [det])
                
                # Build frame path (no stream_id subdirectory, detections_output_dir already stream-specific)
                timestamp = int(time.time())
                frame_filename = f"frame_{timestamp}_{det.object_type}.jpg"
                frame_path = Path(self.detections_output_dir) / "annotated" / frame_filename
                
                self.frame_annotator.save_frame(
                    annotated_frame,
                    str(frame_path),
                    quality=self.config.annotation_quality
                )
                
                det.frame_path_annotated = str(frame_path)
                det.segment_index = segment.index
                det.segment_filename = segment.filename
                det.stream_id = self.stream_id
                
                # Save raw frame (always available now)
                raw_path = Path(self.detections_output_dir) / "raw" / frame_filename
                self.frame_annotator.save_frame(frame, str(raw_path), quality=95)
                det.frame_path_raw = str(raw_path)
                
                # Store in database
                try:
                    det_id = self.db.insert_detection(det.to_dict())
                    logger.debug(f"Stored detection: {det.object_type} ({det.confidence:.2f}) -> ID {det_id}")
                except Exception as e:
                    logger.error(f"Failed to store detection: {e}")
                
                # Clean up annotated frame to free memory
                del annotated_frame
        
        finally:
            # Always clean up the source frame after processing
            del frame
    
    def start(self):
        """Start worker thread."""
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()
        logger.info(f"DetectionWorker[{self.stream_id}] thread started")
    
    def stop(self):
        """Stop worker thread gracefully."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)
        
        # Clean up resources
        try:
            self.db.close()
        except Exception as e:
            logger.warning(f"Error closing database: {e}")
        
        logger.info(f"DetectionWorker[{self.stream_id}] stopped")
        if self.thread:
            self.thread.join(timeout=5)
        logger.info("DetectionWorker stopped")
    
    def get_statistics(self):
        """Get detection statistics."""
        return self.db.get_statistics()
