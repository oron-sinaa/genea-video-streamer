"""Entry point for multi-stream AI inference service."""

import sys
import os
import logging
import signal
from pathlib import Path

from ai_inference.config import load_config
from ai_inference.worker_pool import DetectionWorkerPool

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def main():
    """Run the detection worker pool."""
    # Parse config path from environment, args, or common locations
    config_path = None
    
    # 1. Check environment variable
    if 'AI_INFERENCE_CONFIG' in os.environ:
        config_path = os.environ['AI_INFERENCE_CONFIG']
    # 2. Check command-line args
    elif len(sys.argv) > 1:
        config_path = sys.argv[1]
    # 3. Try common locations
    else:
        for path in [
            'config/inference.yaml',
            '/app/config/inference.yaml',
            '/etc/streamer/inference.yaml',
            '/etc/ai_inference/config.yaml',
        ]:
            if Path(path).exists():
                config_path = path
                break
    
    if not config_path:
        logger.error("No config file found. Set AI_INFERENCE_CONFIG or provide path as argument.")
        sys.exit(1)
    
    logger.info(f"Loading configuration from: {config_path}")
    
    try:
        config = load_config(config_path)
    except Exception as e:
        logger.error(f"Failed to load config: {e}")
        sys.exit(1)
    
    # Create and start worker pool
    try:
        pool = DetectionWorkerPool(config)
    except Exception as e:
        logger.error(f"Failed to initialize worker pool: {e}")
        sys.exit(1)
    
    # Setup signal handlers for graceful shutdown
    def signal_handler(signum, frame):
        logger.info(f"Received signal {signum}, shutting down...")
        pool.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)
    
    # Start pool and run
    try:
        pool.start()
        
        # Keep pool running
        while True:
            import time
            time.sleep(1)
    
    except KeyboardInterrupt:
        logger.info("Keyboard interrupt, shutting down...")
        pool.stop()
    
    except Exception as e:
        logger.error(f"Unexpected error: {e}", exc_info=True)
        pool.stop()
        sys.exit(1)


if __name__ == '__main__':
    main()
