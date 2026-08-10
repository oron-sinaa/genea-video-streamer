"""Query interface and HTTP API for detections."""

import json
from pathlib import Path
from typing import List, Dict, Any, Optional
from datetime import datetime, timedelta
import time
import mimetypes

from ai_inference.database import DetectionDatabase


class DetectionSearchAPI:
    """Query interface for detection searches."""
    
    def __init__(self, db_path: str):
        """Initialize with database."""
        self.db = DetectionDatabase(db_path)
    
    def search_by_time_range(self, start_ts: int, end_ts: int) -> List[Dict]:
        """
        Search detections by timestamp range.
        
        Args:
            start_ts: Unix timestamp start
            end_ts: Unix timestamp end
            
        Returns:
            List of detection records
        """
        return self.db.get_detections_by_time(start_ts, end_ts)
    
    def search_by_object_type(self, obj_type: str, limit: int = 100) -> List[Dict]:
        """Search detections by class (person, car)."""
        return self.db.get_detections_by_object_type(obj_type, limit)
    
    def search_recent(self, hours: int = 1, limit: int = 100) -> List[Dict]:
        """Search last N hours of detections."""
        end_ts = int(time.time())
        start_ts = end_ts - (hours * 3600)
        
        detections = self.db.get_detections_by_time(start_ts, end_ts)
        return detections[:limit]
    
    def get_detection(self, det_id: int) -> Optional[Dict]:
        """Get single detection by ID."""
        return self.db.get_detection_by_id(det_id)
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get overall statistics."""
        return self.db.get_statistics()
    
    def get_hourly_statistics(self, hours: int = 24) -> List[Dict]:
        """Get hourly detection counts."""
        end_ts = int(time.time())
        start_ts = end_ts - (hours * 3600)
        
        detections = self.db.get_detections_by_time(start_ts, end_ts)
        
        # Aggregate by hour
        hourly = {}
        for det in detections:
            hour = datetime.fromtimestamp(det['unix_timestamp']).replace(
                minute=0, second=0, microsecond=0
            ).isoformat()
            
            if hour not in hourly:
                hourly[hour] = {'person': 0, 'car': 0, 'total': 0}
            
            hourly[hour][det['object_type']] += 1
            hourly[hour]['total'] += 1
        
        return [{'hour': k, **v} for k, v in sorted(hourly.items())]


class HttpDetectionHandler:
    """HTTP endpoint handlers for detection API."""
    
    def __init__(self, db_path: str, frames_dir: str):
        """Initialize handler."""
        self.api = DetectionSearchAPI(db_path)
        self.frames_dir = Path(frames_dir)
    
    def handle_search_query(self, query_type: str, params: Dict) -> tuple:
        """
        Handle search query.
        
        Returns:
            (status_code, json_response)
        """
        try:
            if query_type == 'recent':
                hours = params.get('hours', 1)
                limit = params.get('limit', 100)
                detections = self.api.search_recent(hours, limit)
                
            elif query_type == 'by_type':
                obj_type = params.get('type', '')
                limit = params.get('limit', 100)
                
                if obj_type not in ['person', 'car']:
                    return (400, {'error': 'Invalid type'})
                
                detections = self.api.search_by_object_type(obj_type, limit)
            
            elif query_type == 'by_time':
                start_ts = params.get('start_ts', 0)
                end_ts = params.get('end_ts', 0)
                detections = self.api.search_by_time_range(start_ts, end_ts)
            
            elif query_type == 'statistics':
                stats = self.api.get_statistics()
                return (200, stats)
            
            elif query_type == 'hourly':
                hours = params.get('hours', 24)
                hourly = self.api.get_hourly_statistics(hours)
                return (200, hourly)
            
            else:
                return (400, {'error': 'Unknown query type'})
            
            return (200, {'detections': detections, 'count': len(detections)})
        
        except Exception as e:
            return (500, {'error': str(e)})
    
    def handle_frame_request(self, frame_path: str) -> tuple:
        """
        Serve frame file.
        
        Returns:
            (status_code, content_type, file_bytes or None, error_message)
        """
        try:
            # Security: prevent path traversal
            requested = Path(frame_path).resolve()
            base = self.frames_dir.resolve()
            
            if not str(requested).startswith(str(base)):
                return (403, '', None, 'Access denied')
            
            if not requested.exists():
                return (404, '', None, 'Frame not found')
            
            # Read and return file
            content_type = mimetypes.guess_type(str(requested))[0] or 'image/jpeg'
            
            with open(requested, 'rb') as f:
                content = f.read()
            
            return (200, content_type, content, None)
        
        except Exception as e:
            return (500, '', None, str(e))
    
    def handle_delete_detection(self, det_id: int) -> tuple:
        """Delete detection by ID."""
        try:
            # Get detection to find frame path
            det = self.api.get_detection(det_id)
            if not det:
                return (404, {'error': 'Detection not found'})
            
            # Delete frame files if they exist
            if det.get('frame_path_annotated'):
                try:
                    Path(det['frame_path_annotated']).unlink()
                except:
                    pass
            
            if det.get('frame_path_raw'):
                try:
                    Path(det['frame_path_raw']).unlink()
                except:
                    pass
            
            # Delete from DB
            self.api.db.delete_detection(det_id)
            
            return (200, {'message': 'Detection deleted'})
        
        except Exception as e:
            return (500, {'error': str(e)})
