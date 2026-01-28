#!/usr/bin/env python3
"""
SQL Variable Tracking Example

Demonstrates how to track SQL queries and detect which columns are being modified.
Works with any Python SQL library (PyMySQL, psycopg2, sqlite3, etc.)
"""

import sqlite3
import sys
from pathlib import Path

# Add memwatch to path
sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent.parent / 'python'))

from memwatch.sql_tracker import init_sql_tracking, track_sql_query, get_sql_tracker


def example_with_sqlite():
    """Example using SQLite (built-in)"""
    print("=" * 60)
    print("SQL Variable Tracking - SQLite Example")
    print("=" * 60)
    print()
    
    # Initialize SQL tracking
    tracker = init_sql_tracking()
    
    # Connect to database
    conn = sqlite3.connect(':memory:')
    cursor = conn.cursor()
    
    # Create table
    create_sql = '''
    CREATE TABLE users (
        id INTEGER PRIMARY KEY,
        name TEXT,
        email TEXT,
        age INTEGER
    )
    '''
    print(f"📝 Executing: {create_sql}")
    cursor.execute(create_sql)
    
    # Track INSERT query
    insert_sql = "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)"
    print(f"\n📝 Executing: {insert_sql}")
    cursor.execute(insert_sql)
    
    changes = track_sql_query(
        insert_sql,
        rows_affected=cursor.rowcount,
        database='main',
        new_value="('Alice', 'alice@example.com', 30)"
    )
    
    print(f"✓ SQL Variables Changed: {len(changes)}")
    for change in changes:
        print(f"  - {change.table_name}.{change.column_name} [{change.operation}]")
    
    # Track UPDATE query
    update_sql = "UPDATE users SET email = 'alice.new@example.com', age = 31 WHERE name = 'Alice'"
    print(f"\n📝 Executing: {update_sql}")
    cursor.execute(update_sql)
    
    changes = track_sql_query(
        update_sql,
        rows_affected=cursor.rowcount,
        database='main',
        old_value="alice@example.com",
        new_value="alice.new@example.com"
    )
    
    print(f"✓ SQL Variables Changed: {len(changes)}")
    for change in changes:
        print(f"  - {change.table_name}.{change.column_name} = {change.new_value}")
    
    # Track more INSERTs
    for i, (name, email) in enumerate([
        ('Bob', 'bob@example.com'),
        ('Charlie', 'charlie@example.com'),
        ('Diana', 'diana@example.com')
    ], 2):
        insert_sql = f"INSERT INTO users (name, email, age) VALUES ('{name}', '{email}', {25 + i})"
        cursor.execute(insert_sql)
        
        changes = track_sql_query(
            insert_sql,
            rows_affected=cursor.rowcount,
            database='main',
            new_value=f"({name}, {email})"
        )
        print(f"  ✓ Inserted {name}: {len(changes)} variables tracked")
    
    # Track SELECT (reads)
    select_sql = "SELECT * FROM users WHERE age > 28"
    print(f"\n📝 Executing: {select_sql}")
    cursor.execute(select_sql)
    rows = cursor.fetchall()
    
    changes = track_sql_query(
        select_sql,
        rows_affected=len(rows),
        database='main'
    )
    
    print(f"✓ SQL Variables Accessed: {len(changes)}")
    for change in changes:
        print(f"  - {change.table_name}.{change.column_name}")
    
    # Track DELETE
    delete_sql = "DELETE FROM users WHERE name = 'Charlie'"
    print(f"\n📝 Executing: {delete_sql}")
    cursor.execute(delete_sql)
    
    changes = track_sql_query(
        delete_sql,
        rows_affected=cursor.rowcount,
        database='main'
    )
    
    print(f"✓ SQL Variables Deleted: {len(changes)}")
    for change in changes:
        print(f"  - {change.table_name}.{change.column_name}")
    
    conn.close()
    
    # Print summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    
    tracker = get_sql_tracker()
    summary = tracker.summary()
    
    print(f"\nTotal tracked changes: {summary['total']}")
    print(f"\nChanges by table:")
    for table, count in summary['tables'].items():
        print(f"  {table}: {count}")
    
    print(f"\nChanges by operation:")
    for op, count in summary['operations'].items():
        print(f"  {op}: {count}")
    
    print(f"\nVariables (columns) changed:")
    for var in sorted(summary['columns_changed']):
        print(f"  - {var}")
    
    print(f"\nDetailed changes:")
    for change in tracker.changes:
        print(f"  [{change.operation}] {change.table_name}.{change.column_name} "
              f"@ {change.timestamp_ns // 1_000_000_000}")


def example_manual_tracking():
    """Example of manual SQL tracking (for when using external DB)"""
    print("\n" + "=" * 60)
    print("Manual SQL Tracking Example")
    print("=" * 60)
    print()
    
    tracker = init_sql_tracking()
    
    # Simulate tracking external database operations
    print("Simulating PostgreSQL operations...")
    
    queries = [
        ("INSERT INTO products (name, price) VALUES ('Widget', 9.99)", 1),
        ("UPDATE products SET price = 12.99 WHERE name = 'Widget'", 1),
        ("INSERT INTO products (name, price) VALUES ('Gadget', 19.99)", 1),
        ("DELETE FROM products WHERE price > 15", 1),
        ("SELECT * FROM products", 2),
    ]
    
    for query, rows in queries:
        print(f"\n📝 {query[:50]}...")
        changes = track_sql_query(query, rows_affected=rows, database='postgres_prod')
        print(f"   Variables affected: {len(changes)}")
        for change in changes:
            print(f"     - {change.table_name}.{change.column_name} [{change.operation}]")
    
    # Show table changes
    print("\n" + "-" * 60)
    print("Changes by table:")
    print("-" * 60)
    
    table_changes = tracker.get_table_changes('products')
    for column, changes_list in table_changes.items():
        print(f"\n{column}: {len(changes_list)} changes")
        for change in changes_list[-3:]:  # Show last 3
            print(f"  - {change.operation}: {change.new_value}")


if __name__ == "__main__":
    example_with_sqlite()
    example_manual_tracking()
    
    print("\n✅ SQL Variable Tracking Examples Complete!")
