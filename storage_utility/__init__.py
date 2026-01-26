# storage_utility/__init__.py
"""Fast mmap-backed storage engine with native C++ implementation."""

from typing import Optional
try:
    from . import _faststorage
except ImportError as e:
    raise ImportError(
        "Failed to import _faststorage extension. "
        "Please build the extension by running build.py"
    ) from e


class FastStorage:
    """Python wrapper for the native FastStorage implementation."""
    
    def __init__(self, filename: str, size: int = 100 * 1024 * 1024):
        if not filename:
            raise ValueError("Filename cannot be empty")
        if size < 1024:
            raise ValueError("Size must be at least 1024 bytes")
        
        self._native = _faststorage.NativeFastStorage(filename, size)
        self._filename = filename
    
    def write(self, key: str, value: str) -> None:
        if not isinstance(key, str):
            raise TypeError("Key must be a string")
        if not isinstance(value, str):
            raise TypeError("Value must be a string")
        if not key:
            raise ValueError("Key cannot be empty")
        
        try:
            self._native.write(key, value)
        except RuntimeError as e:
            raise RuntimeError(f"Write failed: {e}") from e
    
    def read(self, key: str) -> str:
        if not isinstance(key, str):
            raise TypeError("Key must be a string")
        if not key:
            raise ValueError("Key cannot be empty")
        
        try:
            return self._native.read(key)
        except RuntimeError as e:
            if "not found" in str(e).lower():
                raise KeyError(f"Key not found: {key}") from e
            raise RuntimeError(f"Read failed: {e}") from e
    
    def delete(self, key: str) -> None:
        if not isinstance(key, str):
            raise TypeError("Key must be a string")
        if not key:
            raise ValueError("Key cannot be empty")
        
        try:
            self._native.remove(key)
        except RuntimeError as e:
            if "not found" in str(e).lower():
                raise KeyError(f"Key not found: {key}") from e
            raise RuntimeError(f"Delete failed: {e}") from e
    
    def flush(self) -> None:
        try:
            self._native.flush()
        except RuntimeError as e:
            raise RuntimeError(f"Flush failed: {e}") from e
    
    def __len__(self) -> int:
        return self._native.size()
    
    def __contains__(self, key: str) -> bool:
        try:
            self._native.read(key)
            return True
        except:
            return False
    
    def __getitem__(self, key: str) -> str:
        return self.read(key)
    
    def __setitem__(self, key: str, value: str) -> None:
        self.write(key, value)
    
    def __delitem__(self, key: str) -> None:
        self.delete(key)
    
    @property
    def bytes_used(self) -> int:
        return self._native.bytes_used()
    
    @property
    def capacity(self) -> int:
        return self._native.capacity()
    
    @property
    def utilization(self) -> float:
        cap = self.capacity
        if cap == 0:
            return 0.0
        return (self.bytes_used / cap) * 100.0


__all__ = ['FastStorage']
