"""AI Inference module for object detection in video streams."""

from ai_inference.config import AiInferenceConfig, StreamConfig, load_config, get_default_config
from ai_inference.detection import Detection, SegmentInfo
from ai_inference.detection_worker import DetectionWorker
from ai_inference.worker_pool import DetectionWorkerPool

__version__ = "0.1.0"

__all__ = [
    'AiInferenceConfig',
    'StreamConfig',
    'load_config',
    'get_default_config',
    'Detection',
    'SegmentInfo',
    'DetectionWorker',
    'DetectionWorkerPool',
    'config',
    'database',
    'detector',
    'frame_annotator',
    'frame_extractor',
    'hls_reader',
    'search_api',
]
