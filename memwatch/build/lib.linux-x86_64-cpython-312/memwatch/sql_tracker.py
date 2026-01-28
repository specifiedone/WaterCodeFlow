"""
SQL Variable Tracker - Detects column changes in SQL queries

Intercepts and parses SQL statements to identify which columns are being modified.
Works with popular Python SQL libraries (PyMySQL, psycopg2, sqlite3, etc.)
"""

import re
import json
import time
from pathlib import Path
from typing import List, Dict, Set, Optional, Tuple
from dataclasses import dataclass, asdict
from datetime import datetime

@dataclass
class SQLChange:
    """Represents a single SQL variable (column) change"""
    timestamp_ns: int
    table_name: str
    column_name: str
    operation: str  # INSERT, UPDATE, DELETE, SELECT
    old_value: Optional[str]
    new_value: Optional[str]
    rows_affected: int
    query_hash: str
    full_query: str
    database: Optional[str] = None
    
    def to_json(self) -> str:
        return json.dumps(asdict(self), default=str)


class SQLParser:
    """Parse SQL queries and extract variable (column) changes"""
    
    # Regex patterns for common SQL operations
    UPDATE_PATTERN = re.compile(
        r'UPDATE\s+(`?[\w\-]+`?)\s+SET\s+(.+?)(?:WHERE|$)',
        re.IGNORECASE
    )
    
    INSERT_PATTERN = re.compile(
        r'INSERT\s+INTO\s+(`?[\w\-]+`?)\s*\(([^)]+)\)\s*VALUES',
        re.IGNORECASE
    )
    
    DELETE_PATTERN = re.compile(
        r'DELETE\s+FROM\s+(`?[\w\-]+`?)',
        re.IGNORECASE
    )
    
    SELECT_PATTERN = re.compile(
        r'SELECT\s+(.+?)\s+FROM\s+(`?[\w\-]+`?)',
        re.IGNORECASE
    )
    
    SET_PATTERN = re.compile(
        r'(`?[\w\-]+`?)\s*=\s*([^,]+)',
        re.IGNORECASE
    )
    
    @staticmethod
    def normalize_query(query: str) -> str:
        """Normalize query for parsing"""
        # Remove extra whitespace and newlines
        query = ' '.join(query.split())
        # Remove backticks and quotes for parsing
        return query.strip()
    
    @classmethod
    def parse_update(cls, query: str) -> Optional[Tuple[str, List[str]]]:
        """Parse UPDATE query to extract table and columns"""
        normalized = cls.normalize_query(query)
        match = cls.UPDATE_PATTERN.search(normalized)
        
        if not match:
            return None
        
        table_name = match.group(1).strip('`"')
        set_clause = match.group(2).strip()
        
        # Parse SET clause for column names
        columns = []
        for set_match in cls.SET_PATTERN.finditer(set_clause):
            col_name = set_match.group(1).strip('`"')
            columns.append(col_name)
        
        return table_name, columns
    
    @classmethod
    def parse_insert(cls, query: str) -> Optional[Tuple[str, List[str]]]:
        """Parse INSERT query to extract table and columns"""
        normalized = cls.normalize_query(query)
        match = cls.INSERT_PATTERN.search(normalized)
        
        if not match:
            return None
        
        table_name = match.group(1).strip('`"')
        columns_str = match.group(2)
        
        # Split column names
        columns = [col.strip().strip('`"') for col in columns_str.split(',')]
        
        return table_name, columns
    
    @classmethod
    def parse_delete(cls, query: str) -> Optional[str]:
        """Parse DELETE query to extract table"""
        normalized = cls.normalize_query(query)
        match = cls.DELETE_PATTERN.search(normalized)
        
        if not match:
            return None
        
        return match.group(1).strip('`"')
    
    @classmethod
    def parse_select(cls, query: str) -> Optional[Tuple[str, List[str]]]:
        """Parse SELECT query to extract table and columns"""
        normalized = cls.normalize_query(query)
        match = cls.SELECT_PATTERN.search(normalized)
        
        if not match:
            return None
        
        columns_str = match.group(1).strip()
        table_name = match.group(2).strip('`"')
        
        # Parse column list
        if columns_str == '*':
            columns = ['*']
        else:
            columns = [col.strip().strip('`"') for col in columns_str.split(',')]
        
        return table_name, columns
    
    @classmethod
    def detect_operation(cls, query: str) -> str:
        """Detect SQL operation type"""
        normalized = cls.normalize_query(query).upper()
        
        if normalized.startswith('INSERT'):
            return 'INSERT'
        elif normalized.startswith('UPDATE'):
            return 'UPDATE'
        elif normalized.startswith('DELETE'):
            return 'DELETE'
        elif normalized.startswith('SELECT'):
            return 'SELECT'
        else:
            return 'UNKNOWN'


class SQLTracker:
    """Track SQL variable (column) changes"""
    
    def __init__(self, storage_path: Optional[str] = None):
        self.storage_path = storage_path
        self.parser = SQLParser()
        self.changes: List[SQLChange] = []
        self._query_hashes: Set[str] = set()
        
        if storage_path:
            self.storage_file = Path(storage_path).parent / f"{Path(storage_path).stem}_sql_changes.jsonl"
        else:
            self.storage_file = None
    
    def _hash_query(self, query: str) -> str:
        """Create hash of query for deduplication"""
        import hashlib
        return hashlib.md5(query.encode()).hexdigest()[:8]
    
    def track_query(
        self,
        query: str,
        rows_affected: int = 0,
        database: Optional[str] = None,
        old_value: Optional[str] = None,
        new_value: Optional[str] = None
    ) -> List[SQLChange]:
        """
        Track a SQL query and extract column changes
        
        Args:
            query: SQL query string
            rows_affected: Number of rows affected by query
            database: Database name
            old_value: Previous value (for updates)
            new_value: New value (for updates)
        
        Returns:
            List of SQLChange objects
        """
        operation = self.parser.detect_operation(query)
        query_hash = self._hash_query(query)
        timestamp_ns = int(time.time() * 1_000_000_000)
        
        changes = []
        
        # Parse based on operation
        if operation == 'UPDATE':
            result = self.parser.parse_update(query)
            if result:
                table_name, columns = result
                for column in columns:
                    change = SQLChange(
                        timestamp_ns=timestamp_ns,
                        table_name=table_name,
                        column_name=column,
                        operation='UPDATE',
                        old_value=old_value,
                        new_value=new_value,
                        rows_affected=rows_affected,
                        query_hash=query_hash,
                        full_query=query,
                        database=database
                    )
                    changes.append(change)
        
        elif operation == 'INSERT':
            result = self.parser.parse_insert(query)
            if result:
                table_name, columns = result
                for column in columns:
                    change = SQLChange(
                        timestamp_ns=timestamp_ns,
                        table_name=table_name,
                        column_name=column,
                        operation='INSERT',
                        old_value=None,
                        new_value=new_value,
                        rows_affected=rows_affected,
                        query_hash=query_hash,
                        full_query=query,
                        database=database
                    )
                    changes.append(change)
        
        elif operation == 'DELETE':
            table_name = self.parser.parse_delete(query)
            if table_name:
                change = SQLChange(
                    timestamp_ns=timestamp_ns,
                    table_name=table_name,
                    column_name='*',  # All columns affected
                    operation='DELETE',
                    old_value=old_value,
                    new_value=None,
                    rows_affected=rows_affected,
                    query_hash=query_hash,
                    full_query=query,
                    database=database
                )
                changes.append(change)
        
        elif operation == 'SELECT':
            result = self.parser.parse_select(query)
            if result:
                table_name, columns = result
                for column in columns:
                    change = SQLChange(
                        timestamp_ns=timestamp_ns,
                        table_name=table_name,
                        column_name=column,
                        operation='SELECT',
                        old_value=None,
                        new_value=new_value,
                        rows_affected=rows_affected,
                        query_hash=query_hash,
                        full_query=query,
                        database=database
                    )
                    changes.append(change)
        
        # Store changes
        self.changes.extend(changes)
        
        # Persist to file if configured
        if self.storage_file:
            self._persist_changes(changes)
        
        return changes
    
    def _persist_changes(self, changes: List[SQLChange]) -> None:
        """Persist changes to JSONL file"""
        try:
            with open(self.storage_file, 'a') as f:
                for change in changes:
                    f.write(change.to_json() + '\n')
        except Exception as e:
            print(f"Error persisting SQL changes: {e}")
    
    def get_changes(
        self,
        table_filter: Optional[str] = None,
        column_filter: Optional[str] = None,
        operation_filter: Optional[str] = None
    ) -> List[SQLChange]:
        """Query tracked changes with filters"""
        results = self.changes
        
        if table_filter:
            results = [c for c in results if c.table_name.lower() == table_filter.lower()]
        
        if column_filter:
            results = [c for c in results if c.column_name.lower() == column_filter.lower()]
        
        if operation_filter:
            results = [c for c in results if c.operation == operation_filter]
        
        return results
    
    def get_table_changes(self, table_name: str) -> Dict[str, List[SQLChange]]:
        """Get all changes for a specific table, grouped by column"""
        changes = self.get_changes(table_filter=table_name)
        grouped = {}
        
        for change in changes:
            if change.column_name not in grouped:
                grouped[change.column_name] = []
            grouped[change.column_name].append(change)
        
        return grouped
    
    def summary(self) -> Dict:
        """Get summary of tracked changes"""
        if not self.changes:
            return {'total': 0, 'tables': {}, 'operations': {}}
        
        summary = {
            'total': len(self.changes),
            'tables': {},
            'operations': {},
            'columns_changed': set()
        }
        
        for change in self.changes:
            # Count by table
            if change.table_name not in summary['tables']:
                summary['tables'][change.table_name] = 0
            summary['tables'][change.table_name] += 1
            
            # Count by operation
            if change.operation not in summary['operations']:
                summary['operations'][change.operation] = 0
            summary['operations'][change.operation] += 1
            
            # Track columns
            summary['columns_changed'].add(f"{change.table_name}.{change.column_name}")
        
        summary['columns_changed'] = list(summary['columns_changed'])
        return summary


# Global tracker instance
_sql_tracker: Optional[SQLTracker] = None


def init_sql_tracking(storage_path: Optional[str] = None) -> SQLTracker:
    """Initialize global SQL tracker"""
    global _sql_tracker
    _sql_tracker = SQLTracker(storage_path)
    return _sql_tracker


def track_sql_query(
    query: str,
    rows_affected: int = 0,
    database: Optional[str] = None,
    old_value: Optional[str] = None,
    new_value: Optional[str] = None
) -> List[SQLChange]:
    """Track a SQL query globally"""
    global _sql_tracker
    if _sql_tracker is None:
        _sql_tracker = SQLTracker()
    return _sql_tracker.track_query(query, rows_affected, database, old_value, new_value)


def get_sql_tracker() -> SQLTracker:
    """Get global SQL tracker"""
    global _sql_tracker
    if _sql_tracker is None:
        _sql_tracker = SQLTracker()
    return _sql_tracker
