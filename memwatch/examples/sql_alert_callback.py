#!/usr/bin/env python3
"""
SQL Variable Tracking Callback

This callback monitors and alerts on SQL variable changes.
Designed to work with memwatch CLI: 
  memwatch_cli run python3 app.py --user-func sql_alert.py --user-func-lang python
"""

import json
import sys
from pathlib import Path
from datetime import datetime

# Add memwatch to path
sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent.parent / 'python'))

from memwatch.sql_tracker import get_sql_tracker, track_sql_query


# Sensitive tables and columns that should trigger alerts
SENSITIVE_COLUMNS = {
    'users': {'password', 'email', 'ssn', 'phone'},
    'payments': {'credit_card', 'amount', 'status'},
    'accounts': {'balance', 'account_number', 'pin'},
    'products': {'price', 'inventory'},
}


def is_sensitive_change(table: str, column: str) -> bool:
    """Check if a table.column is sensitive"""
    if table in SENSITIVE_COLUMNS:
        return column in SENSITIVE_COLUMNS[table]
    return False


def main():
    """
    Callback function - called whenever memory changes
    Can also be used to track SQL operations
    """
    
    # Find event data
    event_files = list(Path("/tmp").glob("memwatch_event_*.json"))
    if not event_files:
        return
    
    try:
        latest_file = max(event_files, key=lambda p: p.stat().st_mtime)
        event_data = json.load(open(latest_file))
        
        # Log the event
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        print(f"[{timestamp}] Memory changed: {event_data['variable']}")
        
        # Check if this is a sensitive operation
        if "sql" in event_data.get('variable', '').lower():
            print(f"  ⚠️  SQL OPERATION DETECTED")
        
    except Exception as e:
        print(f"Error in callback: {e}")


def track_database_change(table: str, column: str, operation: str, 
                          old_value=None, new_value=None):
    """Helper to track database changes and alert on sensitive ones"""
    
    # Track the SQL change
    tracker = get_sql_tracker()
    
    # Build a fake SQL query for tracking
    if operation == 'UPDATE':
        sql = f"UPDATE {table} SET {column} = '{new_value}'"
    elif operation == 'INSERT':
        sql = f"INSERT INTO {table} ({column}) VALUES ('{new_value}')"
    elif operation == 'DELETE':
        sql = f"DELETE FROM {table}"
    else:
        sql = f"SELECT * FROM {table}"
    
    changes = track_sql_query(sql, rows_affected=1, old_value=old_value, new_value=new_value)
    
    # Alert on sensitive changes
    if is_sensitive_change(table, column):
        timestamp = datetime.now().isoformat()
        print(f"\n🚨 SECURITY ALERT - {timestamp}")
        print(f"   Operation: {operation} on {table}.{column}")
        print(f"   Old: {old_value}")
        print(f"   New: {new_value}")
        print(f"   Variables tracked: {len(changes)}")
        
        # You could also:
        # - Send alert email
        # - Log to file
        # - Send to monitoring system
        # - Trigger audit trail
        
        return True
    
    return False


if __name__ == "__main__":
    main()
