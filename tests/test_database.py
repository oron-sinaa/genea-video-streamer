#!/usr/bin/env python3
"""Test database creation and schema validation."""

import sys
import os
import sqlite3
import tempfile
from pathlib import Path

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

from ai_inference.database import DetectionDatabase, SCHEMA
from ai_inference.detection import Detection
import time


def test_database_creation():
    """Test database file creation and initialization."""
    print("\n=== Test 1: Database Creation ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, 'test.db')
        
        # Create database
        db = DetectionDatabase(db_path)
        print(f"✓ Database created at {db_path}")
        
        # Check file exists
        assert os.path.exists(db_path), "Database file not created"
        print(f"✓ Database file exists (size: {os.path.getsize(db_path)} bytes)")
        
        # Check WAL file (journal mode)
        assert os.path.exists(db_path + '-wal') or os.path.exists(db_path + '-shm'), \
            "WAL/SHM files should exist"
        print("✓ WAL mode enabled (journal files found)")
        
        db.close()
        print("✓ Database connection closed")


def test_schema_validation():
    """Test that schema tables are created correctly."""
    print("\n=== Test 2: Schema Validation ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, 'test.db')
        db = DetectionDatabase(db_path)
        
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # Check detections table
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='detections'")
        assert cursor.fetchone(), "detections table not created"
        print("✓ detections table exists")
        
        # Check detection_stats table
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='detection_stats'")
        assert cursor.fetchone(), "detection_stats table not created"
        print("✓ detection_stats table exists")
        
        # Check indexes
        cursor.execute("SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'idx_%'")
        indexes = cursor.fetchall()
        assert len(indexes) >= 4, f"Expected at least 4 indexes, found {len(indexes)}"
        print(f"✓ {len(indexes)} indexes created")
        
        # Verify column names
        cursor.execute("PRAGMA table_info(detections)")
        columns = {row[1] for row in cursor.fetchall()}
        required_cols = {'object_type', 'confidence', 'bbox_x', 'bbox_y', 'bbox_w', 'bbox_h',
                        'unix_timestamp', 'segment_filename', 'frame_path_annotated'}
        assert required_cols.issubset(columns), f"Missing columns: {required_cols - columns}"
        print(f"✓ All required columns exist ({len(columns)} total)")
        
        conn.close()
        db.close()


def test_insert_detection():
    """Test inserting a detection record."""
    print("\n=== Test 3: Insert Detection ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, 'test.db')
        db = DetectionDatabase(db_path)
        
        # Create detection
        det = Detection(
            object_type='person',
            confidence=0.95,
            bbox_x=0.1,
            bbox_y=0.2,
            bbox_w=0.3,
            bbox_h=0.4,
            unix_timestamp=int(time.time()),
            segment_filename='segment_000001.ts',
            segment_index=1,
            stream_id='camera-1'
        )
        
        # Insert
        det_id = db.insert_detection(det.to_dict())
        assert det_id > 0, "Insert returned invalid ID"
        print(f"✓ Detection inserted with ID {det_id}")
        
        # Verify insertion
        result = db.get_detection_by_id(det_id)
        assert result is not None, "Failed to retrieve inserted detection"
        assert result['object_type'] == 'person', "Object type mismatch"
        assert result['confidence'] == 0.95, "Confidence mismatch"
        print(f"✓ Detection retrieved successfully")
        print(f"  - Type: {result['object_type']}")
        print(f"  - Confidence: {result['confidence']}")
        print(f"  - Bbox: ({result['bbox_x']}, {result['bbox_y']}) size ({result['bbox_w']}x{result['bbox_h']})")
        
        db.close()


def test_query_operations():
    """Test various query operations."""
    print("\n=== Test 4: Query Operations ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, 'test.db')
        db = DetectionDatabase(db_path)
        
        # Insert multiple detections
        now = int(time.time())
        detections = [
            {'object_type': 'person', 'confidence': 0.9, 'bbox_x': 0.1, 'bbox_y': 0.2,
             'bbox_w': 0.3, 'bbox_h': 0.4, 'unix_timestamp': now - 3600, 'stream_id': 'camera-1'},
            {'object_type': 'person', 'confidence': 0.85, 'bbox_x': 0.5, 'bbox_y': 0.3,
             'bbox_w': 0.2, 'bbox_h': 0.3, 'unix_timestamp': now - 1800, 'stream_id': 'camera-1'},
            {'object_type': 'car', 'confidence': 0.92, 'bbox_x': 0.3, 'bbox_y': 0.1,
             'bbox_w': 0.4, 'bbox_h': 0.5, 'unix_timestamp': now - 900, 'stream_id': 'camera-1'},
        ]
        
        ids = []
        for det_data in detections:
            det_id = db.insert_detection(det_data)
            ids.append(det_id)
        print(f"✓ Inserted {len(ids)} detections")
        
        # Test time range query
        results = db.get_detections_by_time(now - 4000, now)
        assert len(results) == 3, f"Expected 3 detections, got {len(results)}"
        print(f"✓ Time range query: found {len(results)} detections")
        
        # Test object type query
        persons = db.get_detections_by_object_type('person')
        assert len(persons) == 2, f"Expected 2 persons, got {len(persons)}"
        print(f"✓ Object type query: found {len(persons)} persons")
        
        cars = db.get_detections_by_object_type('car')
        assert len(cars) == 1, f"Expected 1 car, got {len(cars)}"
        print(f"✓ Object type query: found {len(cars)} cars")
        
        # Test statistics
        stats = db.get_statistics()
        assert stats['total_detections'] == 3, f"Expected 3 total, got {stats['total_detections']}"
        assert stats['by_type']['person'] == 2, f"Expected 2 persons in stats"
        assert stats['by_type']['car'] == 1, f"Expected 1 car in stats"
        print(f"✓ Statistics query:")
        print(f"  - Total: {stats['total_detections']}")
        print(f"  - By type: {stats['by_type']}")
        print(f"  - Avg confidence: {stats['average_confidence']:.3f}")
        
        db.close()


def test_delete_operation():
    """Test deletion operations."""
    print("\n=== Test 5: Delete Operation ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, 'test.db')
        db = DetectionDatabase(db_path)
        
        # Insert detection
        det_data = {
            'object_type': 'person',
            'confidence': 0.9,
            'bbox_x': 0.1, 'bbox_y': 0.2, 'bbox_w': 0.3, 'bbox_h': 0.4,
            'unix_timestamp': int(time.time()),
            'stream_id': 'camera-1'
        }
        det_id = db.insert_detection(det_data)
        print(f"✓ Detection inserted with ID {det_id}")
        
        # Verify it exists
        result = db.get_detection_by_id(det_id)
        assert result is not None, "Detection not found"
        
        # Delete it
        success = db.delete_detection(det_id)
        assert success, "Delete operation failed"
        print(f"✓ Detection {det_id} deleted")
        
        # Verify it's gone
        result = db.get_detection_by_id(det_id)
        assert result is None, "Detection should be deleted"
        print(f"✓ Verified detection is gone")
        
        db.close()


def test_cleanup_old_records():
    """Test cleanup of old records."""
    print("\n=== Test 6: Cleanup Old Records ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, 'test.db')
        db = DetectionDatabase(db_path)
        
        now = int(time.time())
        
        # Insert old and new detections
        old_det = {
            'object_type': 'person',
            'confidence': 0.9,
            'bbox_x': 0.1, 'bbox_y': 0.2, 'bbox_w': 0.3, 'bbox_h': 0.4,
            'unix_timestamp': now - (40 * 86400),  # 40 days ago
            'stream_id': 'camera-1'
        }
        new_det = {
            'object_type': 'car',
            'confidence': 0.85,
            'bbox_x': 0.3, 'bbox_y': 0.4, 'bbox_w': 0.3, 'bbox_h': 0.4,
            'unix_timestamp': now - 3600,  # 1 hour ago
            'stream_id': 'camera-1'
        }
        
        db.insert_detection(old_det)
        db.insert_detection(new_det)
        
        stats = db.get_statistics()
        initial_count = stats['total_detections']
        assert initial_count == 2, f"Expected 2 detections, got {initial_count}"
        print(f"✓ Initial count: {initial_count} detections")
        
        # Cleanup records older than 30 days
        deleted_count = db.cleanup_old_records(retention_days=30)
        assert deleted_count == 1, f"Expected 1 deletion, got {deleted_count}"
        print(f"✓ Cleanup deleted {deleted_count} old record(s)")
        
        # Verify only new one remains
        stats = db.get_statistics()
        remaining = stats['total_detections']
        assert remaining == 1, f"Expected 1 remaining, got {remaining}"
        print(f"✓ After cleanup: {remaining} detection(s) remain")
        
        db.close()


def main():
    """Run all tests."""
    print("=" * 60)
    print("AI Inference Database Test Suite")
    print("=" * 60)
    
    tests = [
        test_database_creation,
        test_schema_validation,
        test_insert_detection,
        test_query_operations,
        test_delete_operation,
        test_cleanup_old_records,
    ]
    
    passed = 0
    failed = 0
    
    for test in tests:
        try:
            test()
            passed += 1
        except AssertionError as e:
            print(f"✗ Test failed: {e}")
            failed += 1
        except Exception as e:
            print(f"✗ Test error: {e}")
            import traceback
            traceback.print_exc()
            failed += 1
    
    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)
    
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
