"""Parse HLS manifests and track segment availability."""

from pathlib import Path
from typing import List, Optional
import time
import re
from ai_inference.detection import SegmentInfo


class HlsReader:
    """Read and parse HLS manifest files."""
    
    def __init__(self, hls_dir: str):
        """Initialize reader for HLS directory."""
        self.hls_dir = Path(hls_dir)
        self.last_segment_index = -1
    
    def read_manifest(self, manifest_file: str = 'archive.m3u8') -> List[SegmentInfo]:
        """
        Parse .m3u8 manifest file.
        
        Args:
            manifest_file: 'archive.m3u8' or 'live.m3u8'
            
        Returns:
            List of SegmentInfo objects
        """
        manifest_path = self.hls_dir / manifest_file
        
        if not manifest_path.exists():
            return []
        
        segments = []
        discontinuity = False
        
        with open(manifest_path, 'r') as f:
            lines = f.readlines()
        
        i = 0
        while i < len(lines):
            line = lines[i].strip()
            
            # Check for discontinuity marker
            if line == '#EXT-X-DISCONTINUITY':
                discontinuity = True
                i += 1
                continue
            
            # Parse EXTINF line
            if line.startswith('#EXTINF:'):
                # Format: #EXTINF:2.0,
                match = re.match(r'#EXTINF:([\d.]+)', line)
                duration = float(match.group(1)) if match else 2.0
                
                # Next line should be segment filename
                i += 1
                if i < len(lines):
                    filename = lines[i].strip()
                    
                    # Extract segment index
                    seg_match = re.match(r'segment_(\d+)\.ts', filename)
                    if seg_match:
                        index = int(seg_match.group(1))
                        filepath = str(self.hls_dir / filename)
                        
                        seg = SegmentInfo(
                            filename=filename,
                            index=index,
                            filepath=filepath,
                            duration=duration,
                            discontinuity=discontinuity
                        )
                        segments.append(seg)
                        discontinuity = False
            
            i += 1
        
        return segments
    
    def get_latest_segment(self) -> Optional[SegmentInfo]:
        """Get most recently created segment."""
        manifest_path = self.hls_dir / 'archive.m3u8'
        
        if not manifest_path.exists():
            return None
        
        segments = self.read_manifest('archive.m3u8')
        if segments:
            return segments[-1]  # Last segment in list
        
        return None
    
    def get_segment_by_index(self, index: int) -> Optional[SegmentInfo]:
        """Get segment by index number."""
        filename = f"segment_{index:06d}.ts"
        filepath = self.hls_dir / filename
        
        if filepath.exists():
            return SegmentInfo(
                filename=filename,
                index=index,
                filepath=str(filepath),
                duration=2.0,
            )
        
        return None
    
    def wait_for_new_segment(self, timeout_s: int = 30) -> Optional[SegmentInfo]:
        """
        Block until new segment appears.
        
        Args:
            timeout_s: Max seconds to wait
            
        Returns:
            New SegmentInfo, or None if timeout
        """
        start_time = time.time()
        
        while True:
            latest = self.get_latest_segment()
            
            if latest and latest.index > self.last_segment_index:
                self.last_segment_index = latest.index
                return latest
            
            # Check timeout
            if time.time() - start_time > timeout_s:
                return None
            
            # Sleep briefly before re-checking
            time.sleep(0.5)
