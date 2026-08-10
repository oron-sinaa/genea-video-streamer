#!/usr/bin/env python3
"""Test YoloDetector model loading and inference."""

import sys
import os
import tempfile
import numpy as np

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

try:
    from ai_inference.detector import YoloDetector
    from ai_inference.detection import Detection
except ImportError as e:
    print(f"Error: Missing module: {e}")
    sys.exit(1)


def create_sample_frame(width=640, height=480):
    """Create a sample BGR image."""
    frame = np.zeros((height, width, 3), dtype=np.uint8)
    # Add some random content
    frame[:, :] = np.random.randint(50, 200, (height, width, 3))
    return frame.astype(np.uint8)


def test_detector_initialization():
    """Test YoloDetector initialization."""
    print("\n=== Test 1: Detector Initialization ===")
    
    try:
        detector = YoloDetector(model_size='n', classes=['person', 'car'], device='cpu')
        print("✓ YoloDetector initialized")
        print(f"  - Model size: nano")
        print(f"  - Target classes: {detector.target_classes}")
        print(f"  - Device: {detector.device}")
    except Exception as e:
        print(f"✗ Failed to initialize: {e}")
        print("  Note: This requires ultralytics package and may download model (~6MB)")
        return False
    
    return True


def test_inference():
    """Test running inference on sample frame."""
    print("\n=== Test 2: Inference ===")
    
    try:
        detector = YoloDetector(model_size='n', classes=['person', 'car'], device='cpu')
        frame = create_sample_frame()
        
        detections = detector.detect(frame, confidence_threshold=0.5)
        
        print(f"✓ Inference completed")
        print(f"  - Input shape: {frame.shape}")
        print(f"  - Detections: {len(detections)}")
        
        for i, det in enumerate(detections):
            print(f"    {i+1}. {det.object_type}: {det.confidence:.2f} @ ({det.bbox_x:.2f}, {det.bbox_y:.2f})")
        
        # Verify detection format
        if detections:
            det = detections[0]
            assert isinstance(det, Detection), "Invalid detection type"
            assert 0 <= det.confidence <= 1, "Invalid confidence"
            assert 0 <= det.bbox_x < 1, "Invalid bbox_x"
            assert 0 <= det.bbox_y < 1, "Invalid bbox_y"
            assert det.object_type in ['person', 'car'], "Invalid object type"
            print("✓ Detection format validated")
        
    except Exception as e:
        print(f"✗ Inference failed: {e}")
        import traceback
        traceback.print_exc()
        return False
    
    return True


def test_confidence_threshold():
    """Test confidence threshold filtering."""
    print("\n=== Test 3: Confidence Threshold ===")
    
    try:
        detector = YoloDetector(model_size='n', device='cpu')
        frame = create_sample_frame()
        
        # Test with low threshold
        detections_low = detector.detect(frame, confidence_threshold=0.3)
        
        # Test with high threshold
        detections_high = detector.detect(frame, confidence_threshold=0.9)
        
        print(f"✓ Threshold filtering works")
        print(f"  - Detections @ 0.3 threshold: {len(detections_low)}")
        print(f"  - Detections @ 0.9 threshold: {len(detections_high)}")
        print(f"  - Lower threshold yields more/equal detections: {len(detections_low) >= len(detections_high)}")
        
        assert len(detections_low) >= len(detections_high), "Threshold filtering broken"
        
    except Exception as e:
        print(f"✗ Threshold test failed: {e}")
        return False
    
    return True


def test_model_sizes():
    """Test different model sizes."""
    print("\n=== Test 4: Model Sizes ===")
    
    sizes_to_test = ['n']  # Start with nano only (smallest)
    
    for size in sizes_to_test:
        try:
            detector = YoloDetector(model_size=size, device='cpu')
            print(f"✓ Loaded model yolov8{size}")
        except Exception as e:
            print(f"✗ Failed to load yolov8{size}: {e}")
            return False
    
    return True


def test_class_filtering():
    """Test that only target classes are returned."""
    print("\n=== Test 5: Class Filtering ===")
    
    try:
        # Only request persons
        detector = YoloDetector(model_size='n', classes=['person'], device='cpu')
        frame = create_sample_frame()
        detections = detector.detect(frame)
        
        for det in detections:
            assert det.object_type == 'person', f"Got unwanted class: {det.object_type}"
        
        print(f"✓ Class filtering works")
        print(f"  - Detector configured for: ['person']")
        print(f"  - All {len(detections)} detections are persons")
        
    except Exception as e:
        print(f"✗ Class filtering failed: {e}")
        return False
    
    return True


def test_benchmark():
    """Benchmark detector performance."""
    print("\n=== Test 6: Benchmark ===")
    
    try:
        detector = YoloDetector(model_size='n', device='cpu')
        stats = detector.benchmark(num_iterations=5)
        
        print(f"✓ Benchmark completed (5 iterations)")
        print(f"  - FPS: {stats['fps']:.1f}")
        print(f"  - Avg latency: {stats['avg_latency_ms']:.1f} ms")
        
        # Sanity checks
        assert stats['fps'] > 0, "FPS must be positive"
        assert stats['avg_latency_ms'] > 0, "Latency must be positive"
        
    except Exception as e:
        print(f"✗ Benchmark failed: {e}")
        return False
    
    return True


def main():
    """Run all tests."""
    print("=" * 60)
    print("YoloDetector Test Suite")
    print("=" * 60)
    print("Note: This test requires ultralytics package installed")
    
    tests = [
        test_detector_initialization,
        test_model_sizes,
        test_class_filtering,
        test_inference,
        test_confidence_threshold,
        test_benchmark,
    ]
    
    passed = 0
    failed = 0
    skipped = 0
    
    for test in tests:
        try:
            result = test()
            if result is False:
                failed += 1
            else:
                passed += 1
        except ModuleNotFoundError as e:
            print(f"⊘ Test skipped (missing dependency): {e}")
            skipped += 1
        except Exception as e:
            print(f"✗ Test error: {e}")
            import traceback
            traceback.print_exc()
            failed += 1
    
    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped")
    print("=" * 60)
    
    if skipped > 0:
        print("\nNote: Tests will run fully when dependencies are installed:")
        print("  pip install -r requirements.txt")
    
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
