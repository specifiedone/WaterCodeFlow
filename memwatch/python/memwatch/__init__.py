"""
memwatch - Language-agnostic memory change watcher

Public API for tracking memory mutations with rich change events including
variable names, file/line locations, old/new values, and minimal overhead.
"""

from dataclasses import dataclass
from typing import Optional, Dict, List, Callable, Any
from enum import Enum
import sys
import ctypes

# Import native core (will be compiled as extension module)
try:
    import _memwatch_native as _native
except ImportError:
    _native = None


class TrackingLevel(Enum):
    """Tracking granularity levels"""
    ALL = "all"
    HEAP = "heap"
    STACK = "stack"
    GLOBALS = "globals"


@dataclass
class ChangeEvent:
    """Event describing a memory mutation"""
    seq: int
    timestamp_ns: int
    adapter_id: int
    region_id: int
    variable_id: Optional[int]
    variable_name: Optional[str]
    where: Dict  # {file, function, line, fault_ip, stack}
    how_big: int
    old_preview: Optional[bytes]
    new_preview: Optional[bytes]
    old_value: Optional[bytes]
    new_value: Optional[bytes]
    storage_key_old: Optional[str]
    storage_key_new: Optional[str]
    metadata: Dict
    
    @classmethod
    def from_dict(cls, d: Dict) -> 'ChangeEvent':
        """Create ChangeEvent from native event dict"""
        return cls(
            seq=d.get('seq', 0),
            timestamp_ns=d.get('timestamp_ns', 0),
            adapter_id=d.get('adapter_id', 0),
            region_id=d.get('region_id', 0),
            variable_id=d.get('variable_id'),
            variable_name=d.get('variable_name'),
            where=d.get('where', {}),
            how_big=d.get('how_big', 0),
            old_preview=d.get('old_preview'),
            new_preview=d.get('new_preview'),
            old_value=d.get('old_value'),
            new_value=d.get('new_value'),
            storage_key_old=d.get('storage_key_old'),
            storage_key_new=d.get('storage_key_new'),
            metadata=d.get('metadata', {})
        )


class MemoryWatcher:
    """
    High-level memory change watcher
    
    Example:
        watcher = MemoryWatcher()
        
        data = bytearray(b"hello")
        region_id = watcher.watch(data, name="my_data")
        
        def on_change(event: ChangeEvent):
            print(f"{event.variable_name} changed at {event.where}")
            print(f"Old: {event.old_value}, New: {event.new_value}")
        
        watcher.set_callback(on_change)
        
        data[0] = ord('H')  # Triggers callback
    """
    
    def __init__(self, adapter: Optional['TrackerAdapter'] = None):
        """
        Initialize watcher
        
        Args:
            adapter: Custom adapter (defaults to best available)
        """
        if _native:
            _native.init()
        
        if adapter is None:
            adapter = _create_default_adapter()
        
        self.adapter = adapter
        self._tracked_objects = {}  # region_id -> (obj, metadata)
        self._callback = None
        
        if _native:
            _native.set_callback(self._handle_native_event)
    
    def watch(self, obj: Any, name: Optional[str] = None, 
              max_value_bytes: int = 256) -> int:
        """
        Watch an object for changes
        
        Args:
            obj: Object to watch (must support buffer protocol)
            name: Variable name (auto-inferred if None)
            max_value_bytes: Max bytes to store per change event
                - 0: No value storage (only metadata)
                - N > 0: Store up to N bytes
                - -1: Store full values (no limit)
                - Default: 256 bytes (minimal overhead)
        
        Returns:
            region_id for later unwatch
        """
        # Auto-infer name from caller's frame if not provided
        if name is None:
            name = _infer_variable_name(obj)
        
        # Get memory view
        try:
            if isinstance(obj, (bytes, bytearray)):
                mem_view = memoryview(obj)
            elif hasattr(obj, '__array_interface__'):
                # NumPy array
                interface = obj.__array_interface__
                mem_view = memoryview(obj)
            else:
                mem_view = memoryview(obj)
        except TypeError:
            raise TypeError(f"Object type {type(obj)} does not support buffer protocol")
        
        # Track with adapter
        metadata = {
            'variable_name': name,
            'type': type(obj).__name__,
            'max_value_bytes': max_value_bytes
        }
        
        region_id = self.adapter.track(obj, metadata)
        self._tracked_objects[region_id] = (obj, metadata)
        
        return region_id
    
    def unwatch(self, region_id: int) -> bool:
        """Stop watching a region"""
        if region_id in self._tracked_objects:
            del self._tracked_objects[region_id]
            return self.adapter.untrack(region_id)
        return False
    
    def all(self, level: TrackingLevel = TrackingLevel.ALL) -> None:
        """
        Watch all variables in calling scope (advanced)
        
        Args:
            level: Which variables to track
        """
        # Get caller's frame
        import inspect
        frame = inspect.currentframe().f_back
        
        if level == TrackingLevel.ALL or level == TrackingLevel.HEAP:
            # Track all local variables that support buffer protocol
            for name, obj in frame.f_locals.items():
                try:
                    self.watch(obj, name=name)
                except (TypeError, AttributeError):
                    pass
    
    def stop_all(self) -> None:
        """Stop watching all regions"""
        for region_id in list(self._tracked_objects.keys()):
            self.unwatch(region_id)
    
    def set_callback(self, fn: Optional[Callable[[ChangeEvent], None]]) -> None:
        """Set callback for change events"""
        self._callback = fn
        
        # For PollingAdapter, we need to set the callback on the adapter itself
        if hasattr(self.adapter, 'set_callback'):
            # Wrap the callback to enrich events
            if fn:
                def wrapped_callback(event_dict):
                    event = ChangeEvent.from_dict(event_dict) if isinstance(event_dict, dict) else event_dict
                    event = self._enrich_event(event)
                    fn(event)
                self.adapter.set_callback(wrapped_callback)
            else:
                self.adapter.set_callback(None)
    
    def check_changes(self) -> List[ChangeEvent]:
        """Synchronously check for changes (polling mode)"""
        events = self.adapter.check_changes()
        return [self._enrich_event(e) for e in events]
    
    def get_stats(self) -> Dict:
        """Get statistics"""
        stats = self.adapter.get_stats()
        stats['tracked_objects'] = len(self._tracked_objects)
        return stats
    
    def _handle_native_event(self, event_dict: Dict) -> None:
        """Handle event from native core"""
        event = ChangeEvent.from_dict(event_dict)
        event = self._enrich_event(event)
        
        if self._callback:
            self._callback(event)
    
    def _enrich_event(self, event: ChangeEvent) -> ChangeEvent:
        """Enrich event with tracked object metadata"""
        if event.region_id in self._tracked_objects:
            obj, metadata = self._tracked_objects[event.region_id]
            if event.variable_name is None:
                event.variable_name = metadata.get('variable_name')
            event.metadata.update(metadata)
        return event


def _infer_variable_name(obj: Any) -> Optional[str]:
    """
    Try to infer variable name from caller's frame
    
    This is best-effort and may not always work.
    """
    import inspect
    import gc
    
    frame = inspect.currentframe()
    if frame is None:
        return None
    
    # Go up two frames (past _infer_variable_name and watch)
    caller_frame = frame.f_back.f_back
    if caller_frame is None:
        return None
    
    # Search locals for object
    for name, value in caller_frame.f_locals.items():
        if value is obj:
            return name
    
    return None


def _create_default_adapter() -> 'TrackerAdapter':
    """Create best available adapter for current platform"""
    from .adapters import MprotectAdapter, PollingAdapter
    
    # Try native mprotect adapter first
    if _native and sys.platform in ('linux', 'darwin'):
        try:
            return MprotectAdapter()
        except Exception:
            pass
    
    # Fall back to polling
    return PollingAdapter()


__all__ = [
    'MemoryWatcher',
    'ChangeEvent',
    'TrackingLevel',
]
