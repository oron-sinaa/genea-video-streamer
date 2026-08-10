#!/usr/bin/env python3
"""Test frame extraction from .ts segments."""

import sys
import os
import tempfile

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

try:
    from ai_inference.frame_extractor import FrameExtractor
except ImportError as e:
    print(f"Error: Missing module: {e}")
    sys.exit(1)


def test_frame_extractor_initialization():
    """Test FrameExtractor initialization."""
    print("\n=== Test 1: FrameExtractor Initialization ===")
    
    extractor = FrameExtractor()
    print("✓ FrameExtractor initialized")


def test_nonexistent_file():
    """Test error handling for missing files."""
    print("\n=== Test 2: Nonexistent File Handling ===")
    
    extractor = FrameExtractor()
    
    try:
        extractor.extract_frame_at_time('/nonexistent/file.ts', 1.0)
        print("✗ Should have raised FileNotFoundError")
        return False
    except FileNotFoundError:
        print("✓ FileNotFoundError raised correctly")
        return True


def test_with_sample_ts_file():
    """Test with an actual .ts file (requires ffmpeg output)."""
    print("\n=== Test 3: Real .ts File (If Available) ===")
    
    # Look for sample .ts files in hls_output
    sample_files = []
    hls_base = os.path.join(os.path.dirname(__file__), '..', 'hls_output')
    
    if os.path.exists(hls_base):
        for root, dirs, files in os.walk(hls_base):
            for f in files:
                if f.endswith('.ts'):
                    sample_files.append(os.path.join(root, f))
                    if len(sample_files) >= 3:
                        break
    
    if not sample_files:
        print("⊘ No .ts files found in hls_output/ (test skipped)")
        print("   This test will run when actual HLS segments are available")
        return True
    
    extractor = FrameExtractor()
    
    for ts_file in sample_files:
        try:
            print(f"\nTesting with: {os.path.basename(ts_file)}")
            
            # Get duration
            duration = extractor.get_segment_duration(ts_file)
            print(f"  - Duration: {duration:.2f} seconds")
            
            if duration > 0:
                # Extract frame at middle
                frame = extractor.extract_frame_at_time(ts_file, duration / 2)
                if frame is not None:
                    print(f"  - Frame shape: {frame.shape}")
                    print(f"  - Frame dtype: {frame.dtype}")
                    print(f"✓ Frame extracted successfully")
                else:
                    print(f"  - Failed to extract frame (may indicate video codec issue)")
            
        except Exception as e:
            print(f"  ✗ Error: {e}")
            return False
    
    return True


def test_extract_interval():
    """Test extracting frames at regular intervals."""
    print("\n=== Test 4: Extract Frames at Interval ===")
    
    hls_base = os.path.join(os.path.dirname(__file__), '..', 'hls_output')
    
    sample_files = []
    if os.path.exists(hls_base):
        for root, dirs, files in os.walk(hls_base):
            for f in files:
                if f.endswith('.ts'):
                    sample_files.append(os.path.join(root, f))
                    break
    
    if not sample_files:
        print("⊘ No .ts files found (test skipped)")
        return True
    
    extractor = FrameExtractor()
    ts_file = sample_files[0]
    
    try:
        frames = extractor.extract_frames_interval(ts_file, interval_s=0.5)
        print(f"✓ Extracted {len(frames)} frames at 0.5s interval")
        
        if frames:
            for i, frame in enumerate(frames[:3]):  # Show first 3
                print(f"  - Frame {i+1}: {frame.shape}")
        
    except Exception as e:
        print(f"✗ Error: {e}")
        return False
    
    return True


def main():
    """Run all tests."""
    print("=" * 60)
    print("FrameExtractor Test Suite")
    print("=" * 60)
    print("Note: Full tests require actual .ts files from HLS output")
    
    tests = [
        test_frame_extractor_initialization,
        test_nonexistent_file,
        test_with_sample_ts_file,
        test_extract_interval,
    ]
    
    passed = 0
    failed = 0
    
    for test in tests:
        try:
            result = test()
            if result is False:
                failed += 1
            else:
                passed += 1
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
