# memwatch Quick Reference

## Installation

```bash
cd memwatch
python3 setup.py build_ext --inplace
python3 setup.py install

# Or use Makefile
make build
make install
```

## Quick Start (30 seconds)

```python
from memwatch import MemoryWatcher

watcher = MemoryWatcher()

# Track a buffer
data = bytearray(b"Hello")
watcher.watch(data, name="data")

# Set callback
def on_change(event):
    print(f"{event.variable_name} changed!")
    print(f"Old: {event.old_value}")
    print(f"New: {event.new_value}")

watcher.set_callback(on_change)

# Modify - callback fires!
data[0] = ord('J')
```

## Common Use Cases

### Track NumPy Array
```python
import numpy as np
array = np.zeros((1000, 1000))
watcher.watch(array, name="my_array")
```

### Track Multiple Buffers
```python
buffers = [bytearray(100) for _ in range(10)]
for i, buf in enumerate(buffers):
    watcher.watch(buf, name=f"buf_{i}")
```

### Auto-track Everything
```python
watcher.all()  # Watches all variables in scope
```

### Get Statistics
```python
stats = watcher.get_stats()
print(f"Tracking {stats['tracked_regions']} regions")
print(f"Memory: {stats['native_memory_bytes']:,} bytes")
```

## Event Fields

```python
def on_change(event):
    event.seq              # Sequence number
    event.timestamp_ns     # When (nanoseconds)
    event.variable_name    # Variable name
    event.where            # {file, function, line, fault_ip}
    event.how_big          # Size in bytes
    
    # For small values (≤ 4KB):
    event.old_value        # Full old value
    event.new_value        # Full new value
    
    # For large values (> 4KB):
    event.storage_key_old  # Storage key
    event.storage_key_new  # Storage key
    
    # Always available:
    event.old_preview      # First 256 bytes
    event.new_preview      # First 256 bytes
```

## Running Examples

```bash
# Small buffer demo (inline values)
python3 examples/small_buffer_demo.py

# Large buffer demo (storage keys)
python3 examples/large_buffer_demo.py

# Or use Makefile
make demo
make examples
```

## Running Tests

```bash
# Individual tests
python3 tests/page_sharing_test.py
python3 tests/overflow_test.py
python3 tests/integration_test.py

# All tests
make test
```

## File Structure

```
memwatch/
├── src/
│   └── memwatch.c              # Native core (signal handler, worker, ring)
├── python/memwatch/
│   ├── __init__.py             # Public API (MemoryWatcher, ChangeEvent)
│   ├── adapters.py             # Adapters (Mprotect, Polling, Trace)
│   └── architecture.md         # Complete architecture documentation
├── examples/
│   ├── small_buffer_demo.py    # Demo: small buffers with inline values
│   └── large_buffer_demo.py    # Demo: large buffers with storage keys
├── tests/
│   ├── page_sharing_test.py    # Test: multiple regions per page
│   ├── overflow_test.py        # Test: ring overflow handling
│   └── integration_test.py     # Test: all features
├── setup.py                     # Build configuration
├── Makefile                     # Build automation
└── README.md                    # Full documentation
```

## Architecture Overview

```
Memory Write
    ↓
Page Fault (SIGSEGV)
    ↓
Signal Handler (O(1), async-safe)
    ↓
Ring Buffer (65K entries)
    ↓
Worker Thread (async)
    ↓
Hash Check → Storage → Callback
```

## Performance

- **No writes**: 0% overhead (protected pages)
- **First write to page**: ~500ns (signal)
- **Subsequent writes**: ~0ns (writable window)
- **Per-region metadata**: ~96 bytes
- **Ring buffer**: ~2 MB (65K entries)

## Configuration

Edit `src/memwatch.c`:

```c
#define RING_CAPACITY 65536           // Ring size
#define WRITABLE_WINDOW_MS 5          // Coalescing window
#define SMALL_COPY_THRESHOLD 4096     // Inline threshold
#define PREVIEW_SIZE 256              // Preview size
```

## Platform Support

| Platform | Mechanism | Overhead |
|----------|-----------|----------|
| Linux    | mprotect  | ~500ns   |
| macOS    | mprotect  | ~500ns   |
| Windows  | Polling   | ~0.1-1%  |

## Troubleshooting

### "No events received"
- Check: Is callback set? `watcher.set_callback(fn)`
- Check: Is object tracked? `region_id = watcher.watch(obj)`
- Check: Add `time.sleep(0.1)` after modification for worker to process

### "Dropped events"
- Solution: Increase ring size or use fewer regions
- Check: `stats['dropped_events']`

### "Build errors"
- Ensure Python dev headers: `apt-get install python3-dev`
- Ensure pthread: `apt-get install build-essential`

## Key Constraints

1. **Objects must support buffer protocol** (bytes, bytearray, numpy, memoryview)
2. **Objects must stay alive** (keep references to prevent GC)
3. **Page granularity** (4KB pages, multiple regions may share)
4. **Ring can overflow** (check `dropped_events` in stats)

## Advanced: Custom Adapter

```python
from memwatch.adapters import TrackerAdapter

class MyAdapter(TrackerAdapter):
    def track(self, obj, metadata):
        # Register with native core
        # Return region_id
        pass
    
    def untrack(self, region_id):
        # Unregister
        pass
    
    def check_changes(self):
        # Optional: sync check
        return []
    
    def get_stats(self):
        return {'adapter': 'my_adapter'}

# Use custom adapter
watcher = MemoryWatcher(adapter=MyAdapter())
```

## Links

- Full docs: `python/memwatch/architecture.md`
- API reference: `README.md`
- Examples: `examples/`
- Tests: `tests/`

## License

MIT License
