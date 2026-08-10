"""SQLite database for detection storage and queries."""

import sqlite3
import time
from datetime import datetime
from pathlib import Path
from typing import List, Optional, Dict, Any

# Database schema (SQL)
SCHEMA = """
CREATE TABLE IF NOT EXISTS detections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    object_type TEXT NOT NULL,
    confidence REAL NOT NULL,
    bbox_x REAL NOT NULL,
    bbox_y REAL NOT NULL,
    bbox_w REAL NOT NULL,
    bbox_h REAL NOT NULL,
    unix_timestamp INTEGER NOT NULL,
    human_readable_time TEXT,
    segment_index INTEGER,
    segment_filename TEXT,
    frame_number_in_segment INTEGER,
    frame_path_annotated TEXT,
    frame_path_raw TEXT,
    stream_id TEXT,
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);

CREATE TABLE IF NOT EXISTS detection_stats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stream_id TEXT,
    hour_bucket TEXT,
    person_count INTEGER DEFAULT 0,
    car_count INTEGER DEFAULT 0,
    total_detections INTEGER DEFAULT 0,
    avg_confidence REAL,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    UNIQUE(stream_id, hour_bucket)
);

CREATE INDEX IF NOT EXISTS idx_detections_object_type ON detections(object_type);
CREATE INDEX IF NOT EXISTS idx_detections_timestamp ON detections(unix_timestamp);
CREATE INDEX IF NOT EXISTS idx_detections_confidence ON detections(confidence);
CREATE INDEX IF NOT EXISTS idx_detections_segment ON detections(segment_index);
CREATE INDEX IF NOT EXISTS idx_stats_hour ON detection_stats(hour_bucket);
"""


class DetectionDatabase:
    """Database wrapper for detection storage and queries."""
    
    def __init__(self, db_path: str):
        """Initialize database connection and create schema."""
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        
        self.conn = sqlite3.connect(str(self.db_path), check_same_thread=False)
        self.conn.execute("PRAGMA journal_mode=WAL")  # Enable WAL for concurrency
        
        # Create schema
        for statement in SCHEMA.split(';'):
            if statement.strip():
                self.conn.execute(statement)
        self.conn.commit()
    
    def insert_detection(self, detection: Dict[str, Any]) -> int:
        """Insert detection, return row ID."""
        cursor = self.conn.cursor()
        cursor.execute("""
            INSERT INTO detections (
                object_type, confidence,
                bbox_x, bbox_y, bbox_w, bbox_h,
                unix_timestamp, human_readable_time,
                segment_index, segment_filename,
                frame_number_in_segment,
                frame_path_annotated, frame_path_raw,
                stream_id
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            detection['object_type'],
            detection['confidence'],
            detection['bbox_x'],
            detection['bbox_y'],
            detection['bbox_w'],
            detection['bbox_h'],
            detection['unix_timestamp'],
            datetime.fromtimestamp(detection['unix_timestamp']).strftime('%Y-%m-%d %H:%M:%S'),
            detection.get('segment_index'),
            detection.get('segment_filename'),
            detection.get('frame_number_in_segment'),
            detection.get('frame_path_annotated'),
            detection.get('frame_path_raw'),
            detection['stream_id']
        ))
        self.conn.commit()
        return cursor.lastrowid
    
    def get_detections_by_time(self, start_ts: int, end_ts: int) -> List[Dict]:
        """Query detections by timestamp range."""
        cursor = self.conn.cursor()
        cursor.execute("""
            SELECT * FROM detections
            WHERE unix_timestamp BETWEEN ? AND ?
            ORDER BY unix_timestamp DESC
        """, (start_ts, end_ts))
        
        rows = cursor.fetchall()
        columns = [desc[0] for desc in cursor.description]
        return [dict(zip(columns, row)) for row in rows]
    
    def get_detections_by_object_type(self, obj_type: str, limit: int = 100) -> List[Dict]:
        """Query detections by class."""
        cursor = self.conn.cursor()
        cursor.execute("""
            SELECT * FROM detections
            WHERE object_type = ?
            ORDER BY unix_timestamp DESC
            LIMIT ?
        """, (obj_type, limit))
        
        rows = cursor.fetchall()
        columns = [desc[0] for desc in cursor.description]
        return [dict(zip(columns, row)) for row in rows]
    
    def get_detection_by_id(self, det_id: int) -> Optional[Dict]:
        """Get single detection by ID."""
        cursor = self.conn.cursor()
        cursor.execute("SELECT * FROM detections WHERE id = ?", (det_id,))
        row = cursor.fetchone()
        
        if row:
            columns = [desc[0] for desc in cursor.description]
            return dict(zip(columns, row))
        return None
    
    def delete_detection(self, det_id: int) -> bool:
        """Delete detection by ID."""
        cursor = self.conn.cursor()
        cursor.execute("DELETE FROM detections WHERE id = ?", (det_id,))
        self.conn.commit()
        return cursor.rowcount > 0
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get overall statistics."""
        cursor = self.conn.cursor()
        
        cursor.execute("SELECT COUNT(*) FROM detections")
        total = cursor.fetchone()[0]
        
        cursor.execute("SELECT object_type, COUNT(*) FROM detections GROUP BY object_type")
        by_type = dict(cursor.fetchall())
        
        cursor.execute("SELECT AVG(confidence) FROM detections")
        avg_conf = cursor.fetchone()[0] or 0.0
        
        return {
            'total_detections': total,
            'by_type': by_type,
            'average_confidence': avg_conf
        }
    
    def cleanup_old_records(self, retention_days: int) -> int:
        """Delete detection records older than N days."""
        retention_seconds = retention_days * 86400
        cutoff_ts = int(time.time()) - retention_seconds
        
        cursor = self.conn.cursor()
        cursor.execute("DELETE FROM detections WHERE unix_timestamp < ?", (cutoff_ts,))
        self.conn.commit()
        
        return cursor.rowcount
    
    def close(self):
        """Close database connection."""
        self.conn.close()
