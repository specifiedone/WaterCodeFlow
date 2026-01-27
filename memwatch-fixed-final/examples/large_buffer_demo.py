#!/usr/bin/env python3
"""
Large Buffer Demo - memwatch

Demonstrates tracking large buffers (> 4KB) where full values are stored
in FastStorage rather than included inline.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from memwatch import MemoryWatcher, ChangeEvent
import time

# Mock numpy for demo (use real numpy if available)
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("Note: numpy not available, using large bytearray instead\n")

def main():
    print("=== memwatch Large Buffer Demo ===\n")
    
    # Create watcher
    watcher = MemoryWatcher()
    
    # Set up callback
    def on_change(event: ChangeEvent):
        print(f"📊 Large buffer changed!")
        print(f"   Variable: {event.variable_name or 'unnamed'}")
        print(f"   Size: {event.how_big:,} bytes")
        print(f"   Sequence: {event.seq}")
        
        # For large buffers, full values are in storage
        if event.storage_key_new:
            print(f"   Storage key (new): {event.storage_key_new}")
            print(f"   (Use storage_utility.FastStorage.read(key) to retrieve)")
        
        if event.storage_key_old:
            print(f"   Storage key (old): {event.storage_key_old}")
        
        # Preview is always available (first 256 bytes)
        if event.new_preview:
            preview = event.new_preview[:64]
            print(f"   Preview (first 64 bytes): {preview}")
            
            # Show statistics about preview
            if len(event.new_preview) == 256:
                avg = sum(event.new_preview) / len(event.new_preview)
                print(f"   Preview avg value: {avg:.2f}")
        
        print()
    
    watcher.set_callback(on_change)
    
    if HAS_NUMPY:
        # Test with NumPy array
        print("Test 1: Large NumPy array (10 MB)")
        large_array = np.zeros((1024, 1024, 10), dtype=np.uint8)
        print(f"Created array: shape={large_array.shape}, size={large_array.nbytes:,} bytes")
        
        region_id = watcher.watch(large_array, name="large_array")
        print(f"Watching region {region_id}\n")
        
        # Modify a section
        print("Modifying section [100:200, 100:200, :] = 255")
        large_array[100:200, 100:200, :] = 255
        time.sleep(0.2)
        
        # Modify another section
        print("Modifying section [500:600, 500:600, :] = 128")
        large_array[500:600, 500:600, :] = 128
        time.sleep(0.2)
    
    else:
        # Fallback: use large bytearray
        print("Test 1: Large bytearray (10 MB)")
        size_mb = 10
        large_buffer = bytearray(size_mb * 1024 * 1024)
        print(f"Created buffer: {len(large_buffer):,} bytes")
        
        region_id = watcher.watch(large_buffer, name="large_buffer")
        print(f"Watching region {region_id}\n")
        
        # Fill with pattern
        print("Writing pattern to first 1000 bytes")
        for i in range(1000):
            large_buffer[i] = i % 256
        time.sleep(0.2)
        
        # Modify another section
        print("Writing pattern to bytes 500000-501000")
        for i in range(500000, 501000):
            large_buffer[i] = (i * 7) % 256
        time.sleep(0.2)
    
    # Test 2: Multiple large buffers
    print("\nTest 2: Multiple large buffers")
    
    buffers = []
    for i in range(3):
        size = (i + 1) * 1024 * 1024  # 1MB, 2MB, 3MB
        buf = bytearray(size)
        buf[:100] = bytes(f"Buffer {i}".encode() * 10)[:100]
        
        region_id = watcher.watch(buf, name=f"buffer_{i}")
        buffers.append((buf, region_id))
        print(f"Watching buffer_{i}: {size:,} bytes, region={region_id}")
    
    print()
    
    # Modify each buffer
    for i, (buf, region_id) in enumerate(buffers):
        print(f"Modifying buffer_{i}")
        buf[1000] = ord('X') + i
        time.sleep(0.15)
    
    # Test 3: Demonstrate storage key usage
    print("\nTest 3: Storage key demonstration")
    print("(In real usage, retrieve full snapshots via:)")
    print("  from storage_utility import FastStorage")
    print("  old_data = FastStorage.read(event.storage_key_old)")
    print("  new_data = FastStorage.read(event.storage_key_new)")
    print()
    
    # Statistics
    print("=== Statistics ===")
    stats = watcher.get_stats()
    for key, value in stats.items():
        if isinstance(value, (int, float)):
            if isinstance(value, int) and value > 1024:
                print(f"{key}: {value:,}")
            else:
                print(f"{key}: {value}")
        else:
            print(f"{key}: {value}")
    
    # Memory usage estimate
    if 'native_memory_bytes' in stats and 'tracked_regions' in stats:
        if stats['tracked_regions'] > 0:
            per_region = stats['native_memory_bytes'] / stats['tracked_regions']
            print(f"\nPer-region overhead: ~{per_region:.0f} bytes")
    
    print("\n=== Demo Complete ===")
    watcher.stop_all()

if __name__ == '__main__':
    main()
