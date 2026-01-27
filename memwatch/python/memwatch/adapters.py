"""
Adapters for different tracking mechanisms

Adapters must be thin - they only register/unregister regions and provide
resolver functions. All heavy work (hashing, diffing, storage) is in the core.
"""

from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional
import sys
import threading
import time
import ctypes

try:
    import memwatch as _native
except ImportError:
    _native = None


class TrackerAdapter(ABC):
    """
    Base class for tracking adapters
    
    Adapters are language-agnostic thin layers that:
    - Register/unregister memory regions
    - Optionally provide resolver for IP->location mapping
    - Must NOT do heavy work (hashing/diffing/storage)
    """
    
    @abstractmethod
    def track(self, obj: Any, metadata: Dict) -> int:
        """
        Track an object
        
        Args:
            obj: Object to track (must support buffer protocol)
            metadata: User-provided metadata (variable_name, etc.)
        
        Returns:
            region_id assigned by core
        """
        pass
    
    @abstractmethod
    def untrack(self, region_id: int) -> bool:
        """Stop tracking a region"""
        pass
    
    @abstractmethod
    def check_changes(self) -> List['ChangeEvent']:
        """Synchronously check for changes (optional)"""
        pass
    
    @abstractmethod
    def get_stats(self) -> Dict:
        """Get adapter statistics"""
        pass


class MprotectAdapter(TrackerAdapter):
    """
    Native page-protection based adapter (Linux/macOS)
    
    Uses mprotect + SIGSEGV to efficiently detect writes.
    Minimal overhead during normal execution.
    """
    
    def __init__(self):
        if not _native:
            raise RuntimeError("Native memwatch module not available")
        
        self.adapter_id = 1
        self._region_to_obj = {}  # region_id -> (obj, memview)
    
    def track(self, obj: Any, metadata: Dict) -> int:
        """Track object using native mprotect mechanism"""
        # Get memory address
        if isinstance(obj, (bytes, bytearray)):
            mem_view = memoryview(obj)
        elif hasattr(obj, '__array_interface__'):
            # NumPy array
            interface = obj.__array_interface__
            mem_view = memoryview(obj)
        else:
            mem_view = memoryview(obj)
        
        # Get buffer info
        buf_info = mem_view.obj if hasattr(mem_view, 'obj') else obj
        
        # For bytearray/array, get actual memory address
        if isinstance(obj, bytearray):
            addr = id(obj) + sys.getsizeof(bytearray()) - len(obj)
            # More accurate: use ctypes
            arr = (ctypes.c_ubyte * len(obj)).from_buffer(obj)
            addr = ctypes.addressof(arr)
        elif hasattr(obj, '__array_interface__'):
            addr = obj.__array_interface__['data'][0]
        else:
            # Generic buffer - try to get address
            arr = (ctypes.c_ubyte * len(mem_view)).from_buffer(mem_view)
            addr = ctypes.addressof(arr)
        
        size = len(mem_view)
        
        # Create metadata ref (could be index into metadata table)
        metadata_ref = id(metadata)
        
        # Track with native core
        region_id = _native.track(addr, size, self.adapter_id, metadata_ref)
        
        # Keep reference to prevent GC
        self._region_to_obj[region_id] = (obj, mem_view, metadata)
        
        return region_id
    
    def untrack(self, region_id: int) -> bool:
        """Untrack region"""
        if region_id in self._region_to_obj:
            del self._region_to_obj[region_id]
            _native.untrack(region_id)
            return True
        return False
    
    def check_changes(self) -> List['ChangeEvent']:
        """Not needed for async mode (events via callback)"""
        return []
    
    def get_stats(self) -> Dict:
        """Get statistics from native core"""
        stats = _native.get_stats() if _native else {}
        stats['adapter'] = 'mprotect'
        stats['tracked_in_adapter'] = len(self._region_to_obj)
        return stats


class PollingAdapter(TrackerAdapter):
    """
    Polling-based fallback adapter (Windows, or when mprotect unavailable)
    
    Periodically computes checksums to detect changes.
    Higher overhead than mprotect, but works everywhere.
    """
    
    def __init__(self, poll_interval_ms: int = 100):
        self.adapter_id = 2
        self.poll_interval_ms = poll_interval_ms
        
        self._regions = {}  # region_id -> TrackedRegion
        self._next_region_id = 1
        self._lock = threading.Lock()
        
        # Start polling thread
        self._running = True
        self._callback = None
        self._thread = threading.Thread(target=self._poll_loop, daemon=True)
        self._thread.start()
    
    def track(self, obj: Any, metadata: Dict) -> int:
        """Track object with polling"""
        # Get memory view
        if isinstance(obj, (bytes, bytearray)):
            mem_view = memoryview(obj)
        elif hasattr(obj, '__array_interface__'):
            mem_view = memoryview(obj)
        else:
            mem_view = memoryview(obj)
        
        with self._lock:
            region_id = self._next_region_id
            self._next_region_id += 1
            
            # Compute initial hash
            initial_hash = self._hash_bytes(mem_view.tobytes())
            
            self._regions[region_id] = {
                'obj': obj,
                'mem_view': mem_view,
                'metadata': metadata,
                'last_hash': initial_hash,
                'epoch': 0,
                'seq': 0
            }
        
        return region_id
    
    def untrack(self, region_id: int) -> bool:
        """Stop tracking"""
        with self._lock:
            if region_id in self._regions:
                del self._regions[region_id]
                return True
        return False
    
    def check_changes(self) -> List:
        """Manually trigger check (returns events)"""
        events = []
        
        with self._lock:
            for region_id, region in self._regions.items():
                event = self._check_region(region_id, region)
                if event:
                    events.append(event)
        
        return events
    
    def get_stats(self) -> Dict:
        """Get statistics"""
        with self._lock:
            return {
                'adapter': 'polling',
                'tracked_regions': len(self._regions),
                'poll_interval_ms': self.poll_interval_ms
            }
    
    def set_callback(self, callback):
        """Set callback for async events"""
        self._callback = callback
    
    def shutdown(self):
        """Stop polling thread"""
        self._running = False
        self._thread.join()
    
    def _poll_loop(self):
        """Background polling loop"""
        while self._running:
            time.sleep(self.poll_interval_ms / 1000.0)
            
            with self._lock:
                for region_id, region in list(self._regions.items()):
                    event = self._check_region(region_id, region)
                    if event and self._callback:
                        self._callback(event)
    
    def _check_region(self, region_id: int, region: Dict) -> Optional[Dict]:
        """Check single region for changes"""
        try:
            current_bytes = region['mem_view'].tobytes()
            current_hash = self._hash_bytes(current_bytes)
            
            if current_hash != region['last_hash']:
                # Change detected
                size = len(current_bytes)
                preview_size = min(256, size)
                
                event = {
                    'seq': region['seq'],
                    'timestamp_ns': time.time_ns(),
                    'adapter_id': self.adapter_id,
                    'region_id': region_id,
                    'variable_name': region['metadata'].get('variable_name'),
                    'where': {},
                    'how_big': size,
                    'new_preview': current_bytes[:preview_size],
                    'old_preview': None,
                    'new_value': current_bytes if size <= 4096 else None,
                    'old_value': None,
                    'storage_key_old': None,
                    'storage_key_new': None,
                    'metadata': region['metadata']
                }
                
                region['last_hash'] = current_hash
                region['epoch'] += 1
                region['seq'] += 1
                
                return event
        except Exception:
            pass
        
        return None
    
    @staticmethod
    def _hash_bytes(data: bytes) -> int:
        """FNV-1a hash"""
        hash_val = 14695981039346656037
        for byte in data:
            hash_val ^= byte
            hash_val = (hash_val * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return hash_val


class TraceAdapter(TrackerAdapter):
    """
    Optional trace-based adapter using sys.settrace
    
    Can provide exact line-level tracking but with higher overhead.
    Useful for debugging specific code paths.
    """
    
    def __init__(self):
        self.adapter_id = 3
        self._regions = {}
        self._next_region_id = 1
        
        # Install trace hook
        self._old_trace = sys.gettrace()
        sys.settrace(self._trace_hook)
    
    def track(self, obj: Any, metadata: Dict) -> int:
        """Track with tracing"""
        region_id = self._next_region_id
        self._next_region_id += 1
        
        mem_view = memoryview(obj) if not isinstance(obj, memoryview) else obj
        
        self._regions[region_id] = {
            'obj': obj,
            'mem_view': mem_view,
            'metadata': metadata,
            'last_hash': self._hash_bytes(mem_view.tobytes()),
        }
        
        return region_id
    
    def untrack(self, region_id: int) -> bool:
        """Stop tracking"""
        if region_id in self._regions:
            del self._regions[region_id]
            return True
        return False
    
    def check_changes(self) -> List:
        """Check for changes"""
        return []
    
    def get_stats(self) -> Dict:
        """Get statistics"""
        return {
            'adapter': 'trace',
            'tracked_regions': len(self._regions)
        }
    
    def _trace_hook(self, frame, event, arg):
        """Trace hook to detect mutations"""
        # This is a simplified version - real implementation would
        # need to instrument writes more carefully
        return self._trace_hook
    
    @staticmethod
    def _hash_bytes(data: bytes) -> int:
        """FNV-1a hash"""
        hash_val = 14695981039346656037
        for byte in data:
            hash_val ^= byte
            hash_val = (hash_val * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return hash_val


"""
ADAPTER INTEGRATION PATTERNS FOR OTHER LANGUAGES

The adapter pattern is language-agnostic. Here's how to implement adapters
in other languages:

1. IN-PROCESS ADAPTER (C/C++/Rust/Go with FFI):

   // C example
   #include "memwatch.h"
   
   void my_app_init() {
       mw_init();
       mw_register_resolver(MY_ADAPTER_ID, my_resolver);
   }
   
   void track_buffer(void *ptr, size_t size, const char *name) {
       uint32_t metadata = register_metadata(name);  // your metadata table
       uint32_t region_id = mw_track((uint64_t)ptr, size, MY_ADAPTER_ID, metadata);
       // Store region_id for later untrack
   }
   
   // Resolver maps fault_ip -> file/line
   int my_resolver(uintptr_t fault_ip, uint32_t adapter_id,
                   char **file, char **function, int *line, void ***stack) {
       // Use libunwind, backtrace(), or DWARF debug info
       *file = resolve_file(fault_ip);
       *function = resolve_function(fault_ip);
       *line = resolve_line(fault_ip);
       return 0;
   }

2. OUT-OF-PROCESS ADAPTER (Node.js, JVM, etc.):

   // Node.js example
   const dgram = require('dgram');
   const client = dgram.createSocket('udp4');
   
   function trackBuffer(buffer, name) {
       const addr = getBufferAddress(buffer);  // using native addon
       const msg = JSON.stringify({
           cmd: 'track',
           addr: addr,
           size: buffer.length,
           adapter_id: 100,
           metadata: {name}
       });
       client.send(msg, MEMWATCH_CONTROL_PORT, 'localhost');
   }
   
   // memwatch core listens on control socket and calls mw_track

3. RESOLVER SERVICE (out-of-process):

   // Python resolver service example
   import socket
   import json
   
   def resolver_service():
       sock = socket.socket(socket.AF_UNIX)
       sock.bind('/tmp/memwatch_resolver_100.sock')
       sock.listen(1)
       
       while True:
           conn, _ = sock.accept()
           req = json.loads(conn.recv(4096))
           
           # Resolve fault_ip using your runtime's introspection
           response = {
               'file': resolve_file(req['fault_ip']),
               'function': resolve_function(req['fault_ip']),
               'line': resolve_line(req['fault_ip'])
           }
           
           conn.send(json.dumps(response).encode())
           conn.close()

ADAPTER REQUIREMENTS:
- Must be THIN: only register/unregister + optional resolver
- Must NOT do hashing, diffing, snapshotting (core does this)
- Must provide accurate memory address + size
- Should keep objects alive to prevent GC moving them
- Resolver is optional but greatly improves event quality
"""


__all__ = [
    'TrackerAdapter',
    'MprotectAdapter', 
    'PollingAdapter',
    'TraceAdapter',
]
