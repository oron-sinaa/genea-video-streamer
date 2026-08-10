"""Load and validate AI inference configuration from YAML."""

import yaml
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional


@dataclass
class StreamConfig:
    """Configuration for a single stream."""
    
    stream_id: str
    enabled: bool = True
    hls_input_dir: str = ""  # Path to HLS segments (required if enabled)
    detections_output_dir: str = ""  # Path to store detection frames (required if enabled)
    
    def validate(self):
        """Validate stream configuration."""
        if self.enabled:
            if not self.hls_input_dir:
                raise ValueError(f"Stream {self.stream_id}: hls_input_dir required when enabled")
            if not self.detections_output_dir:
                raise ValueError(f"Stream {self.stream_id}: detections_output_dir required when enabled")


@dataclass
class AiInferenceConfig:
    """AI Inference configuration with multi-stream support."""
    
    # Shared inference settings
    model: str = "yolov8n"
    classes: List[str] = field(default_factory=lambda: ['person', 'car'])
    confidence_threshold: float = 0.5
    
    # Inference tuning
    inference_interval_s: int = 5
    frames_per_segment: int = 1
    
    # Database (shared across all streams)
    database_path: str = "/app/detections.db"
    
    # Performance
    max_workers: int = 4
    batch_size: int = 1
    device: str = "cpu"  # 'cpu' or 'cuda'
    
    # Retention & cleanup
    retention_days: int = 30
    
    # Annotation style
    annotate_thickness: int = 2
    annotate_font_scale: float = 0.6
    annotation_quality: int = 85  # JPEG 0-100
    
    # Logging
    log_level: str = "INFO"
    log_detections: bool = True
    
    # Multi-stream configuration
    streams: List[StreamConfig] = field(default_factory=list)
    
    def __post_init__(self):
        """Validate configuration after initialization."""
        # Validate confidence threshold
        if self.confidence_threshold < 0 or self.confidence_threshold > 1:
            raise ValueError("confidence_threshold must be 0-1")
        
        # Validate retention
        if self.retention_days < 1:
            raise ValueError("retention_days must be >= 1")
        
        # Validate streams
        if not self.streams:
            raise ValueError("At least one stream must be configured")
        
        stream_ids = set()
        for stream in self.streams:
            # Check for duplicate stream IDs
            if stream.stream_id in stream_ids:
                raise ValueError(f"Duplicate stream_id: {stream.stream_id}")
            stream_ids.add(stream.stream_id)
            
            # Validate each stream
            stream.validate()
        
        # Check that at least one stream is enabled
        enabled_count = sum(1 for s in self.streams if s.enabled)
        if enabled_count == 0:
            raise ValueError("At least one stream must be enabled")


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
    
    # Parse streams list
    streams_data = ai_config_data.pop('streams', [])
    if not isinstance(streams_data, list):
        raise ValueError("'streams' must be a list")
    
    streams = [
        StreamConfig(
            stream_id=s['stream_id'],
            enabled=s.get('enabled', True),
            hls_input_dir=s.get('hls_input_dir', ''),
            detections_output_dir=s.get('detections_output_dir', '')
        )
        for s in streams_data
    ]
    
    # Create config with streams
    ai_config_data['streams'] = streams
    return AiInferenceConfig(**ai_config_data)


def get_default_config() -> AiInferenceConfig:
    """Return default configuration with example streams."""
    return AiInferenceConfig(
        streams=[
            StreamConfig(
                stream_id='camera-1',
                enabled=True,
                hls_input_dir='/app/hls_output/camera-1',
                detections_output_dir='/app/detections/camera-1'
            )
        ]
    )
