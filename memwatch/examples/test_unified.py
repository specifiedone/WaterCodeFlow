#!/usr/bin/env python3
"""
memwatch - Python example with unified API
Same API and behavior as all other language bindings
"""

from memwatch import MemoryWatcher, ChangeEvent
import time

def main():
    print("MemWatch - Python Unified API Example")
    print("=" * 50)
    
    # Create watcher
    watcher = MemoryWatcher()
    print("✓ MemoryWatcher initialized")
    
    # Create a buffer to watch
    data = bytearray(b"Hello, World!")
    print(f"✓ Created buffer: {data}")
    
    # Watch it
    region_id = watcher.watch(data, name="data")
    print(f"✓ Started watching region {region_id}")
    
    # Set up callback
    events = []
    def on_change(event: ChangeEvent):
        print(f"  → Change detected: {event.variable_name}")
        print(f"    - Where: {event.where}")
        print(f"    - Old: {event.old_preview}")
        print(f"    - New: {event.new_preview}")
        events.append(event)
    
    watcher.set_callback(on_change)
    print("✓ Callback registered")
    
    # Modify data - should trigger callback
    print("\nModifying data...")
    data[0] = ord('J')  # H -> J
    time.sleep(0.1)  # Let callback process
    
    data[1] = ord('e')  # e -> e (keep it simple)
    data[2] = ord('l')
    time.sleep(0.1)
    
    # Check stats
    stats = watcher.get_stats()
    print(f"\nStats:")
    print(f"  - Tracked regions: {stats['tracked_objects']}")
    print(f"  - Total events: {len(events)}")
    
    # Cleanup
    watcher.unwatch(region_id)
    print(f"\n✓ Stopped watching region {region_id}")
    
    # Verify events were captured
    if len(events) > 0:
        print(f"\n✓ SUCCESS: Detected {len(events)} change event(s)")
        return 0
    else:
        print("\n✗ FAILURE: No change events detected")
        return 1

if __name__ == "__main__":
    exit(main())
