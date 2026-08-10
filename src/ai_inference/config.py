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
    
    if data is None:
        raise ValueError(f"Config file is empty: {config_path}")
    
    ai_config_data = data.get('ai_inference', {})
    return AiInferenceConfig(**ai_config_data)


def get_default_config() -> AiInferenceConfig:
    """Return default configuration."""
    return AiInferenceConfig()
