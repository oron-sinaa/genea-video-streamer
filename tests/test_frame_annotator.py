#!/usr/bin/env python3
"""Test FrameAnnotator with sample images."""

import sys
import os
import tempfile
import numpy as np

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

try:
    import cv2
    from ai_inference.frame_annotator import FrameAnnotator
    from ai_inference.detection import Detection
except ImportError as e:
    print(f"Error: Missing required package: {e}")
    print("Install with: pip install opencv-python numpy")
    sys.exit(1)


def create_sample_frame(width=640, height=480, color=(100, 150, 200)):
    """Create a sample BGR image."""
    frame = np.zeros((height, width, 3), dtype=np.uint8)
    # Add gradient
    for y in range(height):
        frame[y, :] = [
            int(color[0] * y / height),
            int(color[1] * y / height),
            int(color[2] * y / height)
        ]
    # Add some shapes
    cv2.circle(frame, (width // 4, height // 4), 50, (255, 0, 0), -1)
    cv2.rectangle(frame, (width // 2 - 40, height // 2 - 30), (width // 2 + 40, height // 2 + 30), (0, 255, 0), -1)
    return frame


def test_annotator_initialization():
    """Test FrameAnnotator initialization."""
    print("\n=== Test 1: Annotator Initialization ===")
    
    annotator = FrameAnnotator(thickness=2, font_scale=0.6)
    assert annotator.thickness == 2, "Thickness mismatch"
    assert annotator.font_scale == 0.6, "Font scale mismatch"
    assert 'person' in annotator.colors, "Person color not defined"
    assert 'car' in annotator.colors, "Car color not defined"
    print("✓ FrameAnnotator initialized successfully")
    print(f"  - Thickness: {annotator.thickness}")
    print(f"  - Font scale: {annotator.font_scale}")
    print(f"  - Colors: {annotator.colors}")


def test_single_detection():
    """Test annotating a frame with one detection."""
    print("\n=== Test 2: Single Detection Annotation ===")
    
    annotator = FrameAnnotator(thickness=2, font_scale=0.6)
    frame = create_sample_frame()
    
    detection = Detection(
        object_type='person',
        confidence=0.95,
        bbox_x=0.2,  # 20% from left
        bbox_y=0.2,  # 20% from top
        bbox_w=0.3,  # 30% width
        bbox_h=0.3,  # 30% height
    )
    
    annotated = annotator.annotate_frame(frame, [detection], draw_confidence=True)
    
    assert annotated.shape == frame.shape, "Output shape mismatch"
    assert annotated.dtype == frame.dtype, "Output dtype mismatch"
    # Check that something changed (pixels were modified)
    assert not np.array_equal(annotated, frame), "Frame was not modified"
    print("✓ Frame annotated successfully")
    print(f"  - Input shape: {frame.shape}")
    print(f"  - Output shape: {annotated.shape}")
    print(f"  - Modified pixels: {np.sum(annotated != frame)} pixels")


def test_multiple_detections():
    """Test annotating a frame with multiple detections."""
    print("\n=== Test 3: Multiple Detections ===")
    
    annotator = FrameAnnotator(thickness=2, font_scale=0.6)
    frame = create_sample_frame()
    
    detections = [
        Detection(
            object_type='person',
            confidence=0.92,
            bbox_x=0.1, bbox_y=0.1, bbox_w=0.2, bbox_h=0.2,
        ),
        Detection(
            object_type='person',
            confidence=0.88,
            bbox_x=0.6, bbox_y=0.3, bbox_w=0.25, bbox_h=0.3,
        ),
        Detection(
            object_type='car',
            confidence=0.95,
            bbox_x=0.3, bbox_y=0.6, bbox_w=0.4, bbox_h=0.25,
        ),
    ]
    
    annotated = annotator.annotate_frame(frame, detections, draw_confidence=True)
    
    assert annotated.shape == frame.shape, "Output shape mismatch"
    print(f"✓ {len(detections)} detections annotated")
    for i, det in enumerate(detections):
        print(f"  - Detection {i+1}: {det.object_type} ({det.confidence*100:.0f}%)")


def test_edge_cases():
    """Test edge cases (bboxes at image boundaries)."""
    print("\n=== Test 4: Edge Cases ===")
    
    annotator = FrameAnnotator(thickness=2, font_scale=0.6)
    frame = create_sample_frame()
    
    detections = [
        # Top-left corner
        Detection(
            object_type='person',
            confidence=0.9,
            bbox_x=0.0, bbox_y=0.0, bbox_w=0.1, bbox_h=0.1,
        ),
        # Bottom-right corner
        Detection(
            object_type='car',
            confidence=0.85,
            bbox_x=0.9, bbox_y=0.9, bbox_w=0.1, bbox_h=0.1,
        ),
        # Spanning full width
        Detection(
            object_type='person',
            confidence=0.8,
            bbox_x=0.0, bbox_y=0.4, bbox_w=1.0, bbox_h=0.1,
        ),
    ]
    
    annotated = annotator.annotate_frame(frame, detections)
    
    assert annotated.shape == frame.shape, "Output shape mismatch"
    print(f"✓ {len(detections)} edge-case detections handled")
    print("  - Top-left corner")
    print("  - Bottom-right corner")
    print("  - Full-width bbox")


def test_frame_saving():
    """Test saving annotated frames to disk."""
    print("\n=== Test 5: Frame Saving ===")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        annotator = FrameAnnotator(thickness=2, font_scale=0.6)
        frame = create_sample_frame()
        
        detection = Detection(
            object_type='person',
            confidence=0.92,
            bbox_x=0.3, bbox_y=0.3, bbox_w=0.4, bbox_h=0.4,
        )
        
        annotated = annotator.annotate_frame(frame, [detection])
        
        # Test with different quality levels
        for quality in [75, 85, 95]:
            output_path = os.path.join(tmpdir, f'frame_quality_{quality}.jpg')
            annotator.save_frame(annotated, output_path, quality=quality)
            
            assert os.path.exists(output_path), f"Frame not saved at {output_path}"
            file_size = os.path.getsize(output_path)
            print(f"✓ Frame saved at quality {quality} ({file_size} bytes)")
            
            # Verify we can read it back
            loaded = cv2.imread(output_path)
            assert loaded is not None, f"Failed to read saved frame"
            assert loaded.shape == frame.shape, f"Shape mismatch after load"
        
        print("✓ All frames saved and verified")


def test_no_confidence_display():
    """Test drawing without confidence labels."""
    print("\n=== Test 6: No Confidence Display ===")
    
    annotator = FrameAnnotator(thickness=2, font_scale=0.6)
    frame = create_sample_frame()
    
    detection = Detection(
        object_type='car',
        confidence=0.88,
        bbox_x=0.2, bbox_y=0.2, bbox_w=0.3, bbox_h=0.3,
    )
    
    # Annotate without confidence
    annotated = annotator.annotate_frame(frame, [detection], draw_confidence=False)
    
    assert annotated.shape == frame.shape, "Output shape mismatch"
    print("✓ Frame annotated without confidence display")


def test_color_mapping():
    """Test that correct colors are used for different object types."""
    print("\n=== Test 7: Color Mapping ===")
    
    annotator = FrameAnnotator()
    
    # Check color values
    assert annotator.colors['person'] == (0, 255, 0), "Person should be green (BGR)"
    assert annotator.colors['car'] == (255, 0, 0), "Car should be blue (BGR)"
    print("✓ Color mapping correct:")
    print(f"  - Person: Green {annotator.colors['person']}")
    print(f"  - Car: Blue {annotator.colors['car']}")


def main():
    """Run all tests."""
    print("=" * 60)
    print("FrameAnnotator Test Suite")
    print("=" * 60)
    
    tests = [
        test_annotator_initialization,
        test_single_detection,
        test_multiple_detections,
        test_edge_cases,
        test_frame_saving,
        test_no_confidence_display,
        test_color_mapping,
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
