"""Entry point for multi-stream AI inference service."""

import sys
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
    # Parse config path from args
    if len(sys.argv) > 1:
        config_path = sys.argv[1]
    else:
        # Try common locations
        for path in [
            'config/ai_inference_config.yaml',
            '/app/config/ai_inference_config.yaml',
            '/etc/ai_inference/config.yaml',
        ]:
            if Path(path).exists():
                config_path = path
                break
        else:
            logger.error("No config file found. Usage: python3 -m ai_inference [config_path]")
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
