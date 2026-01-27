#!/usr/bin/env python3
"""
Small Buffer Demo - memwatch

Demonstrates tracking a small buffer (≤ 4KB) with inline old/new values
and precise location information.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from memwatch import MemoryWatcher, ChangeEvent

def main():
    print("=== memwatch Small Buffer Demo ===\n")
    
    # Create watcher
    watcher = MemoryWatcher()
    
    # Set up callback
    def on_change(event: ChangeEvent):
        print(f"📝 Change detected!")
        print(f"   Variable: {event.variable_name or 'unnamed'}")
        print(f"   Size: {event.how_big} bytes")
        print(f"   Sequence: {event.seq}")
        print(f"   Time: {event.timestamp_ns / 1e9:.6f}s")
        
        if event.where:
            file = event.where.get('file', '?')
            line = event.where.get('line', '?')
            func = event.where.get('function', '?')
            ip = event.where.get('fault_ip', '?')
            print(f"   Where: {file}:{line} in {func}")
            print(f"   Fault IP: {ip}")
        
        # For small buffers, we get full old/new values
        if event.old_value and event.new_value:
            print(f"   Old value: {event.old_value[:50]}")
            print(f"   New value: {event.new_value[:50]}")
        else:
            # Fall back to previews
            if event.new_preview:
                print(f"   New preview: {event.new_preview[:50]}")
        
        print()
    
    watcher.set_callback(on_change)
    
    # Test 1: Track a bytearray
    print("Test 1: Tracking bytearray")
    data = bytearray(b"Hello, World! This is a test buffer.")
    region_id = watcher.watch(data, name="data")
    print(f"Watching region {region_id}: {data[:20]}...\n")
    
    # Make a change
    print("Modifying data[0] = 'J'")
    data[0] = ord('J')
    
    # Give worker thread time to process
    import time
    time.sleep(0.1)
    
    print("Modifying data[7:12] = 'WORLD'")
    data[7:12] = b'WORLD'
    time.sleep(0.1)
    
    # Test 2: Track multiple buffers
    print("\nTest 2: Multiple buffers in same page")
    buffer_a = bytearray(b"Buffer A" * 10)
    buffer_b = bytearray(b"Buffer B" * 10)
    
    id_a = watcher.watch(buffer_a, name="buffer_a")
    id_b = watcher.watch(buffer_b, name="buffer_b")
    
    print(f"Watching buffer_a (id={id_a}) and buffer_b (id={id_b})\n")
    
    print("Modifying buffer_a")
    buffer_a[0] = ord('X')
    time.sleep(0.1)
    
    print("Modifying buffer_b")
    buffer_b[0] = ord('Y')
    time.sleep(0.1)
    
    # Test 3: Rapid changes (writable window test)
    print("\nTest 3: Rapid changes (writable window coalescing)")
    rapid = bytearray(b"rapid" * 20)
    id_rapid = watcher.watch(rapid, name="rapid")
    
    print("Making 5 rapid changes...")
    for i in range(5):
        rapid[i] = ord('0') + i
        # No sleep - should coalesce into fewer events
    
    time.sleep(0.2)  # Wait for worker
    
    # Clean up
    print("\n=== Statistics ===")
    stats = watcher.get_stats()
    for key, value in stats.items():
        print(f"{key}: {value}")
    
    print("\n=== Demo Complete ===")
    watcher.stop_all()

if __name__ == '__main__':
    main()
