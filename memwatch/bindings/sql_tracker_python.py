"""
Universal SQL Tracker - Language-agnostic SQL variable tracking

Works with any database (SQLite, PostgreSQL, MySQL, etc.)
Detects column-level changes from SQL INSERT, UPDATE, DELETE, SELECT queries
"""

import ctypes
import json
import os
import platform
from pathlib import Path
from dataclasses import dataclass
from typing import List, Dict, Optional
from enum import IntEnum

# Load the native SQL tracker library
def _load_sql_tracker():
    """Load the native sql_tracker library"""
    system = platform.system()
    
    if system == "Linux":
        libname = "libsql_tracker.so"
    elif system == "Darwin":
        libname = "libsql_tracker.dylib"
    elif system == "Windows":
        libname = "sql_tracker.dll"
    else:
        raise RuntimeError(f"Unsupported platform: {system}")
    
    # Try multiple search paths
    search_paths = [
        Path(__file__).parent.parent.parent / "build" / libname,
        Path(__file__).parent / libname,
        Path("/usr/local/lib") / libname,
        Path("/usr/lib") / libname,
    ]
    
    for path in search_paths:
        if path.exists():
            return ctypes.CDLL(str(path))
    
    # Try without full path (may be in system library path)
    try:
        if system == "Windows":
            return ctypes.CDLL(libname)
        else:
            return ctypes.CDLL(f"lib{libname[3:]}")
    except OSError:
        raise RuntimeError(f"Could not find {libname}. Make sure to build with: make sql_tracker")

class SQLOperation(IntEnum):
    """SQL operation types"""
    UNKNOWN = 0
    INSERT = 1
    UPDATE = 2
    DELETE = 3
    SELECT = 4

@dataclass
class SQLChange:
    """Single column change from SQL operation"""
    timestamp_ns: int                # Nanosecond timestamp
    table_name: str                  # Table affected
    column_name: str                 # Column affected
    operation: SQLOperation          # INSERT, UPDATE, DELETE, SELECT
    old_value: Optional[str] = None  # Previous value
    new_value: Optional[str] = None  # New value
    rows_affected: int = 0           # Number of rows
    database: Optional[str] = None   # Database name
    full_query: str = ""             # Complete SQL
    
    def to_dict(self):
        """Convert to dictionary"""
        return {
            'timestamp_ns': self.timestamp_ns,
            'table_name': self.table_name,
            'column_name': self.column_name,
            'operation': self.operation.name,
            'old_value': self.old_value,
            'new_value': self.new_value,
            'rows_affected': self.rows_affected,
            'database': self.database,
            'full_query': self.full_query
        }

class SQLTracker:
    """Track SQL column-level changes"""
    
    def __init__(self, storage_path: Optional[str] = None):
        """
        Initialize SQL tracker
        
        Args:
            storage_path: Optional file path to persist changes (JSONL format)
        """
        self._lib = _load_sql_tracker()
        
        # Define C function signatures
        self._lib.sql_tracker_init.restype = ctypes.c_void_p
        self._lib.sql_tracker_init.argtypes = [ctypes.c_char_p]
        
        self._lib.sql_tracker_track_query.restype = ctypes.c_int
        self._lib.sql_tracker_track_query.argtypes = [
            ctypes.c_void_p,  # tracker
            ctypes.c_char_p,  # query
            ctypes.c_int,     # rows_affected
            ctypes.c_char_p,  # database
            ctypes.c_char_p,  # old_value
            ctypes.c_char_p   # new_value
        ]
        
        self._lib.sql_tracker_free.restype = None
        self._lib.sql_tracker_free.argtypes = [ctypes.c_void_p]
        
        # Initialize native tracker
        path_bytes = storage_path.encode() if storage_path else None
        self._tracker = self._lib.sql_tracker_init(path_bytes)
        self._storage_path = storage_path
        self._changes: List[SQLChange] = []
    
    def track_query(self, query: str, rows_affected: int = 0, 
                   database: Optional[str] = None, old_value: Optional[str] = None,
                   new_value: Optional[str] = None) -> List[SQLChange]:
        """
        Track a SQL query and extract column changes
        
        Args:
            query: SQL query string
            rows_affected: Number of rows affected
            database: Database name
            old_value: Previous value (for updates)
            new_value: New value (for inserts/updates)
        
        Returns:
            List of SQLChange objects for affected columns
        """
        query_bytes = query.encode() if query else None
        db_bytes = database.encode() if database else None
        old_bytes = old_value.encode() if old_value else None
        new_bytes = new_value.encode() if new_value else None
        
        # Call native function
        created = self._lib.sql_tracker_track_query(
            self._tracker,
            query_bytes,
            rows_affected,
            db_bytes,
            old_bytes,
            new_bytes
        )
        
        # For now, return empty list (would need to read from C struct)
        # In a full implementation, would read changes from tracker
        return []
    
    def get_changes(self, table_filter: Optional[str] = None,
                   column_filter: Optional[str] = None,
                   operation_filter: Optional[str] = None) -> List[SQLChange]:
        """
        Query tracked changes with filters
        
        Args:
            table_filter: Filter by table name
            column_filter: Filter by column name
            operation_filter: Filter by operation (INSERT, UPDATE, DELETE, SELECT)
        
        Returns:
            List of matching SQLChange objects
        """
        result = []
        for change in self._changes:
            match = True
            if table_filter and change.table_name != table_filter:
                match = False
            if column_filter and change.column_name != column_filter:
                match = False
            if operation_filter and change.operation.name != operation_filter:
                match = False
            
            if match:
                result.append(change)
        
        return result
    
    def summary(self) -> Dict:
        """Get statistics about tracked changes"""
        ops = {}
        tables = {}
        columns = set()
        
        for change in self._changes:
            op_name = change.operation.name
            ops[op_name] = ops.get(op_name, 0) + 1
            
            tables[change.table_name] = tables.get(change.table_name, 0) + 1
            
            columns.add(f"{change.table_name}.{change.column_name}")
        
        return {
            'total': len(self._changes),
            'operations': ops,
            'tables': tables,
            'columns_changed': list(columns)
        }
    
    def __del__(self):
        """Cleanup"""
        if hasattr(self, '_tracker') and self._tracker:
            self._lib.sql_tracker_free(self._tracker)

# Global tracker instance
_global_tracker: Optional[SQLTracker] = None

def init_sql_tracking(storage_path: Optional[str] = None) -> SQLTracker:
    """
    Initialize global SQL tracker
    
    Args:
        storage_path: Optional file path for persistence
    
    Returns:
        SQLTracker instance
    """
    global _global_tracker
    _global_tracker = SQLTracker(storage_path)
    return _global_tracker

def track_sql_query(query: str, rows_affected: int = 0,
                   database: Optional[str] = None, old_value: Optional[str] = None,
                   new_value: Optional[str] = None) -> List[SQLChange]:
    """
    Track query using global tracker
    
    Args:
        query: SQL query string
        rows_affected: Number of rows affected
        database: Database name
        old_value: Previous value
        new_value: New value
    
    Returns:
        List of SQLChange objects
    """
    global _global_tracker
    if not _global_tracker:
        _global_tracker = init_sql_tracking()
    
    return _global_tracker.track_query(query, rows_affected, database, old_value, new_value)

def get_sql_tracker() -> SQLTracker:
    """Get global tracker instance"""
    global _global_tracker
    if not _global_tracker:
        _global_tracker = init_sql_tracking()
    return _global_tracker
