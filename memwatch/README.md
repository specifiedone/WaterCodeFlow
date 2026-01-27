###I got the docs generated from claude. as it is faster than typing them by hand. 

# memwatch

**Language-agnostic memory change watcher** — Deterministically detect mutations in memory buffers, tensors, and byte arrays with rich change events and minimal overhead.

## Overview

memwatch tracks memory mutations at the byte level and delivers detailed change events including:
- **What changed**: Variable name, size, old/new values (or storage keys for large data)
- **Where**: File, function, line number, instruction pointer, optional stack trace
- **When**: Monotonic nanosecond timestamp and sequence number
- **How**: Precise byte-level diffs with efficient previews

**Key Features:**
- ✅ Tiny native overhead: ~96 bytes per tracked region
- ✅ Effectively unnoticeable runtime overhead (uses page protection + async worker)
- ✅ Language-agnostic: Thin adapters for Python, C, C++, Node.js, etc.
- ✅ Safe: Async-signal-safe handler, graceful ring overflow handling
- ✅ Cross-platform: mprotect on Linux/macOS, polling fallback on Windows

## Quick Start

### Installation

```bash
git clone https://github.com/yourusername/memwatch
cd memwatch
python setup.py build_ext --inplace
python setup.py install
```

### Basic Usage

```python
from memwatch import MemoryWatcher, ChangeEvent

# Create watcher
watcher = MemoryWatcher()

# Track a buffer
data = bytearray(b"Hello, World!")
region_id = watcher.watch(data, name="data")

# Set up callback
def on_change(event: ChangeEvent):
    print(f"{event.variable_name} changed at {event.where}")
    print(f"Old: {event.old_value}")
    print(f"New: {event.new_value}")

watcher.set_callback(on_change)

# Modify data - callback will be invoked
data[0] = ord('J')  # Triggers: "data changed at script.py:15"
```

### NumPy Arrays

```python
import numpy as np

# Track large array
array = np.zeros((1000, 1000), dtype=np.float32)
watcher.watch(array, name="array")

# Modifications are automatically detected
array[100:200, 100:200] = 1.0  # Event includes storage key for full snapshot
```

### Auto-tracking

```python
# Watch all variables in scope
watcher.all()

# Now any buffer modification is tracked
buffer_a = bytearray(100)
buffer_b = bytearray(200)

buffer_a[0] = 42  # Detected!
buffer_b[0] = 99  # Detected!
```

## Architecture

```
Memory Write → Page Fault (SIGSEGV) → O(1) Ring Enqueue → Worker Thread
  → Hash Check → Storage → Callback with ChangeEvent
```

Key components:
- **Signal Handler**: Async-signal-safe, O(1) work only (ring write)
- **Worker Thread**: Drains ring, computes hashes, generates events
- **Page Table**: Maps pages → regions to mitigate false positives
- **Ring Buffer**: 65K entry lockfree circular buffer (~2MB)
- **Storage**: Large values (>4KB) written to FastStorage

See [architecture.md](python/memwatch/architecture.md) for details.

## Performance

### Overhead

| Scenario | Overhead |
|----------|----------|
| No writes | 0% (pages protected) |
| First write to page | ~500ns (signal handling) |
| Writes in 5ms window | 0% (page writable) |
| Hash computation | ~10µs per MB |
| Per-region metadata | ~96 bytes |

### Scalability

- 1,000 regions: ~160 KB native memory, <1ms worker latency
- 10,000 regions: ~1.5 MB native memory, ~10ms worker latency
- 100,000 regions: ~15 MB native memory, ~100ms worker latency

### Adaptive Throttling

For hot regions (>100 faults/sec), memwatch automatically:
1. Opens writable windows (5-10ms) to coalesce bursty writes
2. Switches to polling mode to avoid signal thrashing
3. Re-enables protection after cooldown

## Examples

### Small Buffer (inline values)

```python
# examples/small_buffer_demo.py
watcher = MemoryWatcher()

data = bytearray(b"test data")
watcher.watch(data, name="data")

def on_change(event):
    # For small buffers (≤ 4KB), full old/new values are inline
    print(f"Old: {event.old_value}")
    print(f"New: {event.new_value}")
    print(f"Changed at {event.where['file']}:{event.where['line']}")

watcher.set_callback(on_change)
data[0] = ord('T')  # Callback receives full values
```

### Large Buffer (storage keys)

```python
# examples/large_buffer_demo.py
import numpy as np
from storage_utility import FastStorage

watcher = MemoryWatcher()

# 100 MB array
large_array = np.zeros((10000, 10000), dtype=np.uint8)
watcher.watch(large_array, name="large_array")

def on_change(event):
    # For large buffers, values are in storage
    if event.storage_key_new:
        full_snapshot = FastStorage.read(event.storage_key_new)
        print(f"Retrieved {len(full_snapshot):,} bytes from storage")
    
    # Preview is always available (first 256 bytes)
    print(f"Preview: {event.new_preview[:64]}")

watcher.set_callback(on_change)
large_array[1000:2000, :] = 255  # Event includes storage keys
```

### Multiple Regions (page sharing)

```python
# tests/page_sharing_test.py
# Multiple small buffers can share same 4KB page
buffers = [bytearray(256) for _ in range(8)]

for i, buf in enumerate(buffers):
    watcher.watch(buf, name=f"buf_{i}")

# Each buffer gets independent events
buffers[0][0] = 1  # Event for buf_0
buffers[1][0] = 2  # Separate event for buf_1
```

## Language Adapters

### Python (Native)

Included - uses mprotect or polling automatically.

### C/C++ (In-Process)

```c
#include "memwatch.h"

// Initialize
mw_init();
mw_register_resolver(MY_ADAPTER_ID, my_resolver_fn);

// Track buffer
void *buffer = malloc(1024);
uint32_t region_id = mw_track((uint64_t)buffer, 1024, MY_ADAPTER_ID, metadata_ref);

// Writes to buffer are now tracked
memset(buffer, 0xFF, 1024);  // Detected!

// Cleanup
mw_untrack(region_id);
mw_shutdown();
```

### Node.js (IPC)

```javascript
// Thin adapter sends registration to memwatch via socket
const memwatch = require('./memwatch-adapter');

const buffer = Buffer.alloc(1024);
memwatch.track(buffer, "myBuffer");

// Writes detected via IPC
buffer[0] = 42;  // Event sent to memwatch daemon
```

See [architecture.md](python/memwatch/architecture.md) for complete adapter patterns.

## API Reference

### MemoryWatcher

```python
class MemoryWatcher:
    def __init__(self, adapter: Optional[TrackerAdapter] = None)
    def watch(self, obj: Any, name: Optional[str] = None, force_store: bool = False) -> int
    def unwatch(self, region_id: int) -> bool
    def all(self, level: TrackingLevel = TrackingLevel.ALL) -> None
    def stop_all(self) -> None
    def set_callback(self, fn: Callable[[ChangeEvent], None]) -> None
    def check_changes(self) -> List[ChangeEvent]
    def get_stats(self) -> Dict
```

### ChangeEvent

```python
@dataclass
class ChangeEvent:
    seq: int                        # Sequence number
    timestamp_ns: int               # Monotonic nanoseconds
    adapter_id: int                 # Adapter ID
    region_id: int                  # Region ID
    variable_id: Optional[int]      # Variable ID
    variable_name: Optional[str]    # Variable name
    where: Dict                     # {file, function, line, fault_ip, stack}
    how_big: int                    # Size in bytes
    old_preview: Optional[bytes]    # First 256 bytes (before)
    new_preview: Optional[bytes]    # First 256 bytes (after)
    old_value: Optional[bytes]      # Full value if ≤ 4096 bytes
    new_value: Optional[bytes]      # Full value if ≤ 4096 bytes
    storage_key_old: Optional[str]  # Storage key (large values)
    storage_key_new: Optional[str]  # Storage key (large values)
    metadata: Dict                  # Adapter metadata
```

## Configuration

### Ring Size

Default: 65,536 entries (~2MB)

```c
// src/memwatch.c
#define RING_CAPACITY 131072  // Double capacity for high write rates
```

### Writable Window

Default: 5ms (allows bursty writes without repeated faults)

```c
#define WRITABLE_WINDOW_MS 10  // Increase for burstier workloads
```

### Storage Threshold

Default: 4096 bytes (smaller values inline, larger to storage)

```c
#define SMALL_COPY_THRESHOLD 8192  // Increase inline threshold
```

## Platform Support

| Platform | Mechanism | Overhead | Detection Latency |
|----------|-----------|----------|-------------------|
| Linux | mprotect + SIGSEGV | ~500ns/fault | <1ms |
| macOS | mprotect + SIGSEGV | ~500ns/fault | <1ms |
| Windows | Polling fallback | ~0.1-1% CPU | ~100ms |

## Limitations

1. **Page granularity**: mprotect operates on 4KB pages (mitigated by page→regions mapping)
2. **GC movement**: Objects must be pinned (Python: kept in reference; JVM: use DirectByteBuffer)
3. **Ring overflow**: Extreme write rates may drop events (tracked in stats)
4. **Signal safety**: Handler is async-signal-safe (no malloc, no Python API)

See [architecture.md](python/memwatch/architecture.md) for detailed limitations and mitigations.

## Testing

```bash
# Run all tests
python examples/small_buffer_demo.py
python examples/large_buffer_demo.py
python tests/page_sharing_test.py
python tests/overflow_test.py

# Unit tests (if pytest installed)
pytest tests/
```

## License

MIT License - See LICENSE file for details.

## Contributing

Contributions welcome! Please see CONTRIBUTING.md for guidelines.

## Citation

If you use memwatch in research, please cite:

```bibtex
@software{memwatch2026,
  title = {memwatch: Language-agnostic memory change watcher},
  author = {memwatch contributors},
  year = {2026},
  url = {https://github.com/yourusername/memwatch}
}
```