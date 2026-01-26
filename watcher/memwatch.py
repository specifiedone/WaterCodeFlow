"""
Memory Watcher - Python Interface
Hardware-assisted memory tracking with unlimited scalability
"""

import memwatch as _memwatch_c
import ctypes
import sys
import traceback
from typing import Any, Callable, Optional, Dict
import threading
import atexit


class MemoryWatcher:
    """
    High-level interface for tracking Python objects and memory regions.
    
    Features:
    - Unlimited tracking (only limited by configured RAM)
    - Full value capture (no truncation)
    - Automatic callback on changes
    - Thread-safe operations
    - Support for any Python object with buffer protocol
    """
    
    def __init__(self, max_memory_mb: int = 1024, capture_full_values: bool = True):
        """
        Initialize the memory watcher.
        
        Args:
            max_memory_mb: Maximum RAM to use for tracking (default: 1GB)
            capture_full_values: Capture complete values or minimal tracking
        """
        _memwatch_c.configure(max_memory_mb=max_memory_mb, 
                             capture_full_values=capture_full_values)
        
        self._tracked_objects: Dict[int, Any] = {}
        self._object_metadata: Dict[int, dict] = {}
        self._callback: Optional[Callable] = None
        self._lock = threading.Lock()
        
        # Set internal callback
        _memwatch_c.set_callback(self._internal_callback)
        
        # Register cleanup
        atexit.register(self.cleanup)
    
    def _internal_callback(self, tag: str, old_value: bytes, new_value: bytes):
        """Internal callback that handles memory change notifications."""
        if self._callback:
            try:
                self._callback(tag, old_value, new_value)
            except Exception as e:
                print(f"Error in user callback: {e}", file=sys.stderr)
                traceback.print_exc()
    
    def set_callback(self, callback: Callable[[str, bytes, bytes], None]):
        """
        Set a callback function to be called when tracked memory changes.
        
        Args:
            callback: Function with signature (tag, old_value, new_value) -> None
        """
        self._callback = callback
    
    def watch_object(self, obj: Any, name: str) -> bool:
        """
        Watch any Python object for changes.
        
        Supports:
        - PyTorch tensors
        - NumPy arrays
        - bytearrays
        - Any object implementing buffer protocol
        - Custom objects with __array_interface__
        
        Args:
            obj: Object to watch
            name: Logical name/tag for the object
            
        Returns:
            True if tracking started successfully
        """
        try:
            buffer_info = self._get_buffer_info(obj)
            if not buffer_info:
                raise ValueError(f"Object {name} does not support buffer protocol")
            
            addr, size = buffer_info
            
            with self._lock:
                obj_id = id(obj)
                self._tracked_objects[obj_id] = obj
                self._object_metadata[obj_id] = {
                    'name': name,
                    'addr': addr,
                    'size': size,
                    'type': type(obj).__name__
                }
            
            _memwatch_c.track(buffer=addr, size=size, tag=name, py_obj=obj)
            return True
            
        except Exception as e:
            print(f"Failed to watch {name}: {e}", file=sys.stderr)
            return False
    
    def watch_buffer(self, buffer: int, size: int, name: str) -> bool:
        """
        Watch a raw memory buffer directly.
        
        Args:
            buffer: Memory address as integer
            size: Size in bytes
            name: Logical name/tag
            
        Returns:
            True if tracking started successfully
        """
        try:
            _memwatch_c.track(buffer=buffer, size=size, tag=name)
            return True
        except Exception as e:
            print(f"Failed to watch buffer {name}: {e}", file=sys.stderr)
            return False
    
    def unwatch_object(self, obj: Any) -> bool:
        """
        Stop watching an object.
        
        Args:
            obj: Object to stop watching
            
        Returns:
            True if unwatching was successful
        """
        with self._lock:
            obj_id = id(obj)
            if obj_id not in self._tracked_objects:
                return False
            
            metadata = self._object_metadata[obj_id]
            addr = metadata['addr']
            
            try:
                _memwatch_c.untrack(addr)
                del self._tracked_objects[obj_id]
                del self._object_metadata[obj_id]
                return True
            except Exception as e:
                print(f"Failed to unwatch {metadata['name']}: {e}", file=sys.stderr)
                return False
    
    def check_changes(self):
        """
        Check all tracked objects for changes and invoke callbacks.
        
        This should be called periodically or after operations that might
        have modified tracked memory.
        """
        with self._lock:
            for obj_id, metadata in self._object_metadata.items():
                try:
                    _memwatch_c.check_and_reprotect(metadata['addr'])
                except Exception as e:
                    print(f"Error checking {metadata['name']}: {e}", file=sys.stderr)
    
    def get_stats(self) -> dict:
        """Get memory tracking statistics."""
        stats = _memwatch_c.get_stats()
        stats['tracked_objects'] = len(self._tracked_objects)
        return stats
    
    def cleanup(self):
        """Cleanup all tracked regions."""
        with self._lock:
            for obj_id in list(self._tracked_objects.keys()):
                obj = self._tracked_objects[obj_id]
                self.unwatch_object(obj)
    
    @staticmethod
    def _get_buffer_info(obj: Any) -> Optional[tuple]:
        """
        Extract buffer address and size from various object types.
        
        Returns:
            (address, size) tuple or None if not supported
        """
        # Try PyTorch tensor
        if hasattr(obj, 'data_ptr') and hasattr(obj, 'element_size'):
            addr = obj.data_ptr()
            size = obj.numel() * obj.element_size()
            return (addr, size)
        
        # Try NumPy array
        if hasattr(obj, '__array_interface__'):
            interface = obj.__array_interface__
            addr = interface['data'][0]
            size = obj.nbytes
            return (addr, size)
        
        # Try buffer protocol
        try:
            mv = memoryview(obj)
            addr = ctypes.addressof(ctypes.c_char.from_buffer(mv))
            size = mv.nbytes
            return (addr, size)
        except (TypeError, AttributeError):
            pass
        
        # Try ctypes
        if hasattr(obj, '_as_parameter_'):
            try:
                addr = ctypes.addressof(obj)
                size = ctypes.sizeof(obj)
                return (addr, size)
            except (TypeError, AttributeError):
                pass
        
        return None
