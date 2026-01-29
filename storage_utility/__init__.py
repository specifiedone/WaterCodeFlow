# storage_utility/__init__.py
"""Fast mmap-backed storage engine with dual backend support.

Supports:
  1. Pure C implementation (faststorage_c.so) - 1.77x faster, recommended
  2. C++ pybind11 implementation (_faststorage.*) - fallback
"""

import ctypes
import os
from pathlib import Path
from typing import Optional

# Determine which backend to use
_backend = None
_lib = None

def _load_c_backend():
    """Load the optimized Pure C FastStorage backend."""
    global _lib
    
    try:
        # Try to load the C version (recommended, faster)
        lib_path = Path(__file__).parent / "faststorage_c.so"
        if lib_path.exists():
            _lib = ctypes.CDLL(str(lib_path))
            
            # Define function signatures
            _lib.fast_storage_create.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
            _lib.fast_storage_create.restype = ctypes.c_void_p
            
            _lib.fast_storage_destroy.argtypes = [ctypes.c_void_p]
            _lib.fast_storage_destroy.restype = None
            
            _lib.fast_storage_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                                               ctypes.c_char_p, ctypes.c_size_t]
            _lib.fast_storage_write.restype = ctypes.c_int
            
            _lib.fast_storage_read.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                                              ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t)]
            _lib.fast_storage_read.restype = ctypes.c_int
            
            _lib.fast_storage_remove.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
            _lib.fast_storage_remove.restype = ctypes.c_int
            
            _lib.fast_storage_flush.argtypes = [ctypes.c_void_p]
            _lib.fast_storage_flush.restype = None
            
            _lib.fast_storage_contains.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
            _lib.fast_storage_contains.restype = ctypes.c_bool
            
            _lib.fast_storage_size.argtypes = [ctypes.c_void_p]
            _lib.fast_storage_size.restype = ctypes.c_size_t
            
            _lib.fast_storage_bytes_used.argtypes = [ctypes.c_void_p]
            _lib.fast_storage_bytes_used.restype = ctypes.c_size_t
            
            _lib.fast_storage_capacity.argtypes = [ctypes.c_void_p]
            _lib.fast_storage_capacity.restype = ctypes.c_size_t
            
            return "c"
    except Exception as e:
        pass
    
    return None

def _load_cpp_backend():
    """Load the C++ pybind11 FastStorage backend (fallback)."""
    try:
        from . import _faststorage
        return "cpp"
    except ImportError:
        return None

# Try to load backends in order of preference
_backend = _load_c_backend()
if not _backend:
    _backend = _load_cpp_backend()
    if _backend:
        from . import _faststorage

if not _backend:
    raise ImportError(
        "Failed to load FastStorage backend. "
        "Please build the extension by running: bash build.sh"
    )


class FastStorage:
    """Python wrapper for FastStorage with automatic backend selection.
    
    Automatically uses:
      1. Pure C version (faststorage_c.so) if available - 1.77x faster
      2. C++ pybind11 version as fallback
    """
    
    def __init__(self, filename: str, size: int = 100 * 1024 * 1024):
        if not filename:
            raise ValueError("Filename cannot be empty")
        if size < 1024:
            raise ValueError("Size must be at least 1024 bytes")
        
        self._filename = filename
        self._size = size
        self._backend = _backend
        
        if _backend == "c":
            # Pure C backend
            self._storage = _lib.fast_storage_create(filename.encode(), size)
            if not self._storage:
                raise RuntimeError(f"Failed to create FastStorage: {filename}")
        else:
            # C++ pybind11 backend
            self._native = _faststorage.NativeFastStorage(filename, size)
    
    def write(self, key: str, value: str) -> None:
        """Write a key-value pair to storage."""
        if not isinstance(key, str):
            raise TypeError("Key must be a string")
        if not isinstance(value, str):
            raise TypeError("Value must be a string")
        if not key:
            raise ValueError("Key cannot be empty")
        
        try:
            if self._backend == "c":
                key_bytes = key.encode()
                value_bytes = value.encode()
                result = _lib.fast_storage_write(self._storage, key_bytes, len(key_bytes),
                                                value_bytes, len(value_bytes))
                if result < 0:
                    raise RuntimeError(f"Write failed for key: {key}")
            else:
                self._native.write(key, value)
        except RuntimeError as e:
            raise RuntimeError(f"Write failed: {e}") from e
    
    def read(self, key: str) -> str:
        """Read a value from storage by key."""
        if not isinstance(key, str):
            raise TypeError("Key must be a string")
        if not key:
            raise ValueError("Key cannot be empty")
        
        try:
            if self._backend == "c":
                key_bytes = key.encode()
                value_ptr = ctypes.c_char_p()
                value_len = ctypes.c_size_t()
                result = _lib.fast_storage_read(self._storage, key_bytes, len(key_bytes),
                                               ctypes.byref(value_ptr), ctypes.byref(value_len))
                if result < 0:
                    raise KeyError(f"Key not found: {key}")
                if value_ptr and value_len.value > 0:
                    # Copy the data because the pointer is into mmap'd memory
                    return ctypes.string_at(value_ptr, value_len.value).decode()
                return ""
            else:
                return self._native.read(key)
        except KeyError:
            raise
        except RuntimeError as e:
            if "not found" in str(e).lower():
                raise KeyError(f"Key not found: {key}") from e
            raise RuntimeError(f"Read failed: {e}") from e
    
    def delete(self, key: str) -> None:
        """Delete a key from storage."""
        if not isinstance(key, str):
            raise TypeError("Key must be a string")
        if not key:
            raise ValueError("Key cannot be empty")
        
        try:
            if self._backend == "c":
                key_bytes = key.encode()
                result = _lib.fast_storage_remove(self._storage, key_bytes, len(key_bytes))
                if result < 0:
                    raise KeyError(f"Key not found: {key}")
            else:
                self._native.remove(key)
        except KeyError:
            raise
        except RuntimeError as e:
            if "not found" in str(e).lower():
                raise KeyError(f"Key not found: {key}") from e
            raise RuntimeError(f"Delete failed: {e}") from e
    
    def flush(self) -> None:
        """Flush storage to disk."""
        try:
            if self._backend == "c":
                _lib.fast_storage_flush(self._storage)
            else:
                self._native.flush()
        except RuntimeError as e:
            raise RuntimeError(f"Flush failed: {e}") from e
    
    def __len__(self) -> int:
        """Return number of items in storage."""
        if self._backend == "c":
            return _lib.fast_storage_size(self._storage)
        else:
            return self._native.size()
    
    def __contains__(self, key: str) -> bool:
        """Check if key exists in storage."""
        if self._backend == "c":
            key_bytes = key.encode()
            return _lib.fast_storage_contains(self._storage, key_bytes, len(key_bytes))
        else:
            try:
                self._native.read(key)
                return True
            except:
                return False
    
    def __getitem__(self, key: str) -> str:
        """Get value by key using [] operator."""
        return self.read(key)
    
    def __setitem__(self, key: str, value: str) -> None:
        """Set value by key using [] operator."""
        self.write(key, value)
    
    def __delitem__(self, key: str) -> None:
        """Delete key using [] operator."""
        self.delete(key)
    
    @property
    def bytes_used(self) -> int:
        """Return bytes used by stored data."""
        if self._backend == "c":
            return _lib.fast_storage_bytes_used(self._storage)
        else:
            return self._native.bytes_used()
    
    @property
    def capacity(self) -> int:
        """Return total capacity in bytes."""
        if self._backend == "c":
            return _lib.fast_storage_capacity(self._storage)
        else:
            return self._native.capacity()
    
    @property
    def utilization(self) -> float:
        """Return utilization percentage (0-100)."""
        cap = self.capacity
        if cap == 0:
            return 0.0
        return (self.bytes_used / cap) * 100.0
    
    @property
    def backend(self) -> str:
        """Return which backend is being used."""
        return "Pure C (optimized)" if self._backend == "c" else "C++ pybind11"
    
    def __del__(self):
        """Cleanup when object is destroyed."""
        if self._backend == "c" and hasattr(self, '_storage'):
            try:
                _lib.fast_storage_destroy(self._storage)
            except:
                pass


__all__ = ['FastStorage']
