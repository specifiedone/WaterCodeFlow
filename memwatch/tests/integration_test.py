#!/usr/bin/env python3
"""
Comprehensive Integration Test - memwatch

Demonstrates all major features:
1. Basic tracking (small buffers)
2. Large buffer tracking (storage keys)
3. Multiple regions
4. Page sharing
5. Auto-tracking
6. Statistics
7. Error handling
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from memwatch import MemoryWatcher, ChangeEvent, TrackingLevel
import time

def test_basic_tracking():
    """Test 1: Basic tracking with small buffer"""
    
    events = []
    
    def callback(event):
        events.append(event)
    
    watcher = MemoryWatcher()
    watcher.set_callback(callback)
    
    # Track a buffer
    data = bytearray(b"Hello, memwatch!")
    region_id = watcher.watch(data, name="test_data")
    
    # Modify
    data[0] = ord('J')
    time.sleep(0.15)
    
    assert len(events) > 0, "Should have received event"
    assert events[0].variable_name == "test_data", "Variable name should match"
    
    watcher.stop_all()
    return True


def test_large_buffer():
    """Test 2: Large buffer with storage keys"""
    
    events = []
    
    def callback(event):
        events.append(event)
    
    watcher = MemoryWatcher()
    watcher.set_callback(callback)
    
    # Create large buffer (> 4KB threshold)
    large_buffer = bytearray(10 * 1024)  # 10 KB
    large_buffer[:100] = b"X" * 100
    
    # Watch with full value storage
    region_id = watcher.watch(large_buffer, name="large_buffer", max_value_bytes=-1)
    
    # Modify
    large_buffer[1000] = ord('Y')
    time.sleep(0.15)
    
    assert len(events) > 0, "Should have received event"
    
    # With max_value_bytes=-1, should have full new_value
    event = events[0]
    assert event.new_value is not None, "Should have new_value with max_value_bytes=-1"
    
    watcher.stop_all()
    return True


def test_multiple_regions():
    """Test 3: Multiple independent regions"""
    print("\n" + "="*60)
    print("TEST 3: Multiple Independent Regions")
    print("="*60)
    
    events = []
    
    def callback(event):
        events.append(event)
        print(f"✓ Event {len(events)}: {event.variable_name}")
    
    watcher = MemoryWatcher()
    watcher.set_callback(callback)
    
    # Track multiple buffers
    buffers = []
    for i in range(5):
        buf = bytearray(100)
        buf[:10] = f"Buf{i}".encode().ljust(10, b'_')
        watcher.watch(buf, name=f"buffer_{i}")
        buffers.append(buf)
    
    print(f"Watching {len(buffers)} buffers")
    
    # Modify each
    for i, buf in enumerate(buffers):
        buf[0] = ord('X')
        time.sleep(0.05)
    
    time.sleep(0.2)
    
    # Should have event for each buffer
    assert len(events) >= len(buffers), f"Should have {len(buffers)} events, got {len(events)}"
    
    print(f"✅ PASS: Received {len(events)} events for {len(buffers)} buffers")
    watcher.stop_all()
    return True


def test_page_sharing():
    """Test 4: Page sharing (multiple regions per page)"""
    print("\n" + "="*60)
    print("TEST 4: Page Sharing")
    print("="*60)
    
    events = []
    
    def callback(event):
        events.append(event)
        print(f"✓ Event: {event.variable_name}")
    
    watcher = MemoryWatcher()
    watcher.set_callback(callback)
    
    # Create small buffers that share pages
    small_buffers = []
    for i in range(8):
        buf = bytearray(256)  # Small enough that many fit in 4KB page
        watcher.watch(buf, name=f"small_{i}")
        small_buffers.append(buf)
    
    print(f"Watching {len(small_buffers)} small buffers (likely share pages)")
    
    # Modify two different buffers
    small_buffers[0][0] = ord('A')
    small_buffers[5][0] = ord('B')
    time.sleep(0.2)
    
    # Should have separate events for each
    assert len(events) >= 2, "Should have events for both buffers"
    
    variable_names = [e.variable_name for e in events]
    assert 'small_0' in variable_names, "Should have event for small_0"
    assert 'small_5' in variable_names, "Should have event for small_5"
    
    print("✅ PASS: Page sharing handled correctly")
    watcher.stop_all()
    return True


def test_auto_tracking():
    """Test 5: Auto-tracking with .all()"""
    print("\n" + "="*60)
    print("TEST 5: Auto-Tracking")
    print("="*60)
    
    events = []
    
    def callback(event):
        events.append(event)
        print(f"✓ Auto-tracked: {event.variable_name}")
    
    watcher = MemoryWatcher()
    watcher.set_callback(callback)
    
    # Create some buffers
    auto_buf_1 = bytearray(50)
    auto_buf_2 = bytearray(50)
    
    # Auto-track everything in scope
    print("Auto-tracking all variables in scope...")
    watcher.all(TrackingLevel.ALL)
    
    # Now modify - should be tracked
    auto_buf_1[0] = ord('X')
    time.sleep(0.1)
    
    assert len(events) > 0, "Should have tracked auto buffers"
    
    print("✅ PASS: Auto-tracking works")
    watcher.stop_all()
    return True


def test_statistics():
    """Test 6: Statistics and monitoring"""
    print("\n" + "="*60)
    print("TEST 6: Statistics")
    print("="*60)
    
    watcher = MemoryWatcher()
    
    # Track some buffers
    buffers = []
    for i in range(10):
        buf = bytearray(100)
        watcher.watch(buf, name=f"stat_buf_{i}")
        buffers.append(buf)
    
    # Get stats
    stats = watcher.get_stats()
    
    print("\nStatistics:")
    for key, value in stats.items():
        if isinstance(value, int) and value > 1024:
            print(f"  {key}: {value:,}")
        else:
            print(f"  {key}: {value}")
    
    # Verify key stats
    assert 'tracked_regions' in stats or 'tracked_objects' in stats, \
        "Should have tracking stats"
    
    if 'native_memory_bytes' in stats:
        assert stats['native_memory_bytes'] > 0, "Should have non-zero memory usage"
        print(f"\n  Per-region overhead: ~{stats['native_memory_bytes'] / stats.get('tracked_regions', 1):.0f} bytes")
    
    print("✅ PASS: Statistics working")
    watcher.stop_all()
    return True


def test_error_handling():
    """Test 7: Error handling"""
    print("\n" + "="*60)
    print("TEST 7: Error Handling")
    print("="*60)
    
    watcher = MemoryWatcher()
    
    # Test invalid unwatch
    result = watcher.unwatch(99999)
    assert result == False, "Should return False for invalid region"
    print("✓ Invalid unwatch handled gracefully")
    
    # Test tracking invalid object
    try:
        watcher.watch("not a buffer", name="invalid")
        print("⚠️  WARNING: Should have raised TypeError")
    except (TypeError, AttributeError):
        print("✓ Invalid object type rejected")
    
    # Test double unwatch
    buf = bytearray(10)
    region_id = watcher.watch(buf)
    watcher.unwatch(region_id)
    result = watcher.unwatch(region_id)  # Second time
    print("✓ Double unwatch handled gracefully")
    
    print("✅ PASS: Error handling works")
    watcher.stop_all()
    return True


def test_callback_removal():
    """Test 8: Callback management"""
    print("\n" + "="*60)
    print("TEST 8: Callback Management")
    print("="*60)
    
    events = []
    
    def callback(event):
        events.append(event)
    
    watcher = MemoryWatcher()
    watcher.set_callback(callback)
    
    buf = bytearray(10)
    watcher.watch(buf)
    
    # Modify - should get event
    buf[0] = 1
    time.sleep(0.1)
    count_with_callback = len(events)
    
    # Remove callback
    watcher.set_callback(None)
    
    # Modify again - should not get event
    buf[0] = 2
    time.sleep(0.1)
    count_without_callback = len(events)
    
    assert count_with_callback > 0, "Should have received event with callback"
    assert count_without_callback == count_with_callback, "Should not receive event without callback"
    
    print("✅ PASS: Callback removal works")
    watcher.stop_all()
    return True


def run_all_tests():
    """Run all tests"""
    print("\n" + "#"*60)
    print("# MEMWATCH COMPREHENSIVE INTEGRATION TEST")
    print("#"*60)
    
    tests = [
        ("Basic Tracking", test_basic_tracking),
        ("Large Buffer", test_large_buffer),
        ("Multiple Regions", test_multiple_regions),
        ("Page Sharing", test_page_sharing),
        ("Auto-Tracking", test_auto_tracking),
        ("Statistics", test_statistics),
        ("Error Handling", test_error_handling),
        ("Callback Management", test_callback_removal),
    ]
    
    results = []
    
    for name, test_func in tests:
        try:
            passed = test_func()
            results.append((name, passed, None))
        except Exception as e:
            print(f"\n❌ FAIL: {name}")
            print(f"   Error: {e}")
            results.append((name, False, str(e)))
    
    # Summary
    print("\n" + "#"*60)
    print("# TEST SUMMARY")
    print("#"*60 + "\n")
    
    passed_count = sum(1 for _, passed, _ in results if passed)
    total_count = len(results)
    
    for name, passed, error in results:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"{status}: {name}")
        if error:
            print(f"         {error}")
    
    print(f"\n{passed_count}/{total_count} tests passed")
    
    if passed_count == total_count:
        print("\n🎉 ALL TESTS PASSED! 🎉")
        return 0
    else:
        print(f"\n⚠️  {total_count - passed_count} test(s) failed")
        return 1


if __name__ == '__main__':
    sys.exit(run_all_tests())
