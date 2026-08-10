"""Manages multiple detection workers, one per stream."""

import logging
import signal
import time
from typing import Dict
from pathlib import Path

from ai_inference.config import load_config, AiInferenceConfig, StreamConfig
from ai_inference.detection_worker import DetectionWorker

logger = logging.getLogger(__name__)


class DetectionWorkerPool:
    """
    Orchestrates detection workers for multiple streams.
    
    Each enabled stream gets its own worker running in a background thread.
    Workers share the same database but write to separate frame directories.
    """
    
    def __init__(self, config: AiInferenceConfig):
        """
        Initialize worker pool.
        
        Args:
            config: AiInferenceConfig with streams list populated
        """
        self.config = config
        self.workers: Dict[str, DetectionWorker] = {}
        self.running = False
        
        # Validate config
        if not config.streams:
            raise ValueError("No streams configured")
        
        enabled_streams = [s for s in config.streams if s.enabled]
        if not enabled_streams:
            raise ValueError("No enabled streams in configuration")
        
        logger.info(f"Initialized worker pool with {len(enabled_streams)} enabled streams")
    
    def start(self):
        """Start all enabled stream workers."""
        if self.running:
            logger.warning("Worker pool already running")
            return
        
        self.running = True
        logger.info("Starting detection worker pool...")
        
        # Create and start worker for each enabled stream
        for stream_config in self.config.streams:
            if not stream_config.enabled:
                logger.info(f"Stream '{stream_config.stream_id}' is disabled, skipping")
                continue
            
            self._start_worker(stream_config)
        
        logger.info(f"Worker pool started with {len(self.workers)} active workers")
    
    def _start_worker(self, stream_config: StreamConfig):
        """
        Start a detection worker for one stream.
        
        Args:
            stream_config: StreamConfig for this stream
        """
        stream_id = stream_config.stream_id
        
        try:
            # Create stream-specific worker config
            # (reuse global config, but set stream-specific paths)
            worker_config = AiInferenceConfig(
                model=self.config.model,
                classes=self.config.classes,
                confidence_threshold=self.config.confidence_threshold,
                inference_interval_s=self.config.inference_interval_s,
                frames_per_segment=self.config.frames_per_segment,
                database_path=self.config.database_path,
                max_workers=self.config.max_workers,
                batch_size=self.config.batch_size,
                device=self.config.device,
                retention_days=self.config.retention_days,
                annotate_thickness=self.config.annotate_thickness,
                annotate_font_scale=self.config.annotate_font_scale,
                annotation_quality=self.config.annotation_quality,
                log_level=self.config.log_level,
                log_detections=self.config.log_detections,
                streams=[stream_config]  # Single stream for this worker
            )
            
            # Create and start worker
            worker = DetectionWorker(worker_config, stream_config)
            worker.start()
            
            self.workers[stream_id] = worker
            logger.info(f"Started worker for stream '{stream_id}'")
        
        except Exception as e:
            logger.error(f"Failed to start worker for stream '{stream_id}': {e}", exc_info=True)
            self.running = False
            raise
    
    def stop(self):
        """Stop all workers gracefully."""
        if not self.running:
            return
        
        logger.info("Stopping worker pool...")
        self.running = False
        
        stopped = 0
        failed = 0
        
        for stream_id, worker in self.workers.items():
            try:
                worker.stop()
                stopped += 1
                logger.info(f"Stopped worker for stream '{stream_id}'")
            except Exception as e:
                failed += 1
                logger.error(f"Error stopping worker '{stream_id}': {e}")
        
        logger.info(f"Worker pool stopped: {stopped} stopped, {failed} failed")
        self.workers.clear()
    
    def get_statistics(self) -> Dict[str, dict]:
        """
        Get statistics from all workers.
        
        Returns:
            Dict mapping stream_id -> detection statistics
        """
        stats = {}
        for stream_id, worker in self.workers.items():
            try:
                stats[stream_id] = worker.get_statistics()
            except Exception as e:
                logger.warning(f"Failed to get stats for '{stream_id}': {e}")
                stats[stream_id] = {'error': str(e)}
        
        return stats
    
    def is_running(self) -> bool:
        """Check if pool is currently running."""
        return self.running
    
    def get_active_workers(self) -> int:
        """Get count of active workers."""
        return len(self.workers)
