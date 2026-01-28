-- SQL tracker for memwatch - Integration examples
-- 
-- The max_value_bytes parameter works across all language bindings
-- SQL integration allows tracking changes in database values
--
-- Usage:
--   1. Create a tracking table in your database
--   2. Register column(s) to watch via memwatch API
--   3. max_value_bytes parameter applies to stored values

-- ============================================
-- Example 1: PostgreSQL Table for Tracking
-- ============================================
CREATE TABLE memory_changes (
    id BIGSERIAL PRIMARY KEY,
    region_id INTEGER NOT NULL,
    event_seq INTEGER NOT NULL,
    timestamp_ns BIGINT NOT NULL,
    variable_name VARCHAR(255),
    old_value BYTEA,
    new_value BYTEA,
    value_bytes_stored INTEGER,  -- Reflects max_value_bytes
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_region (region_id),
    INDEX idx_timestamp (timestamp_ns)
);

-- ============================================
-- Example 2: Storing Full Values (max_value_bytes=-1)
-- ============================================
INSERT INTO memory_changes (region_id, event_seq, timestamp_ns, variable_name, 
                           old_value, new_value, value_bytes_stored)
VALUES (
    1,
    1,
    1699564800000000000,
    'buffer_var',
    E'\\x0102030405'::bytea,
    E'\\x63020304cc'::bytea,
    5  -- Full 5 bytes stored because max_value_bytes=-1
);

-- ============================================
-- Example 3: Limited Value Storage (max_value_bytes=256)
-- ============================================
INSERT INTO memory_changes (region_id, event_seq, timestamp_ns, variable_name, 
                           old_value, new_value, value_bytes_stored)
VALUES (
    2,
    2,
    1699564800100000000,
    'large_buffer_var',
    -- Large buffer (5000 bytes), but only first 256 bytes stored
    E'\\x' || repeat('00', 256)::text || ''::bytea,
    E'\\x' || repeat('01', 256)::text || ''::bytea,
    256  -- Only first 256 bytes of 5000-byte buffer
);

-- ============================================
-- Example 4: No Value Storage (max_value_bytes=0)
-- ============================================
INSERT INTO memory_changes (region_id, event_seq, timestamp_ns, variable_name, 
                           old_value, new_value, value_bytes_stored)
VALUES (
    3,
    3,
    1699564800200000000,
    'metadata_only_var',
    NULL,  -- No value stored
    NULL,  -- No value stored
    0      -- max_value_bytes=0, metadata only
);

-- ============================================
-- Query Examples
-- ============================================

-- Get all changes for a specific region with full tracking
SELECT region_id, variable_name, 
       OCTET_LENGTH(old_value) as old_size,
       OCTET_LENGTH(new_value) as new_size,
       value_bytes_stored
FROM memory_changes
WHERE region_id = 1
ORDER BY event_seq;

-- Statistics: How much storage used for each region
SELECT region_id, 
       COUNT(*) as num_changes,
       SUM(COALESCE(OCTET_LENGTH(old_value), 0) + 
           COALESCE(OCTET_LENGTH(new_value), 0)) as total_bytes_used,
       AVG(value_bytes_stored) as avg_max_bytes
FROM memory_changes
GROUP BY region_id
ORDER BY total_bytes_used DESC;

-- Find changes where values were limited by max_value_bytes
SELECT * FROM memory_changes
WHERE value_bytes_stored > 0 AND value_bytes_stored < 1000
ORDER BY timestamp_ns DESC;

-- ============================================
-- Integration with memwatch APIs
-- ============================================

/*
Python Example:
    import sqlite3
    from memwatch import MemWatch
    
    watcher = MemWatch()
    # Watch with max_value_bytes=-1 (store full values)
    region_id = watcher.watch(buffer, name="test", max_value_bytes=-1)
    
    # ... modifications happen ...
    
    # Query changes from database
    changes = watcher.check_changes()
    
    # Store in database with value tracking
    conn = sqlite3.connect('tracking.db')
    for change in changes:
        conn.execute('''
            INSERT INTO memory_changes 
            (region_id, event_seq, timestamp_ns, variable_name, 
             old_value, new_value, value_bytes_stored)
            VALUES (?, ?, ?, ?, ?, ?, -1)
        ''', (region_id, change.seq, change.timestamp_ns, 
              change.variable_name, change.old_preview, 
              change.new_preview))

JavaScript Example:
    const MemWatch = require('./memwatch');
    const sqlite3 = require('sqlite3');
    
    const watcher = new MemWatch();
    // Watch with max_value_bytes=256 (limit storage)
    const region_id = watcher.watch(buffer, 'test', 256);
    
    // ... modifications happen ...
    
    const changes = watcher.check_changes();
    const db = new sqlite3.Database('tracking.db');
    
    for (const change of changes) {
        db.run(
            'INSERT INTO memory_changes (...) VALUES (...)',
            [region_id, change.seq, change.timestamp_ns, 
             change.variable_name, change.old_preview, 
             change.new_preview, 256]
        );
    }

Rust Example:
    use memwatch::MemWatch;
    
    let watcher = MemWatch::new()?;
    
    // Watch with max_value_bytes=-1 (full values)
    let region_id = watcher.watch_vec_with_max_value_bytes(
        &buffer, "test", -1
    )?;
    
    let events = watcher.check_changes();
    // Store in SQL database via sql_tracker
*/

-- ============================================
-- SQL Tracker C Header Integration
-- ============================================
/*

#include "sql_tracker.h"

// In your C/C++ code:
// 1. Initialize tracking table
sql_tracker_init("tracking.db");

// 2. Watch memory region with max_value_bytes
sql_tracker_watch(region_id, "variable_name", 256);

// 3. Log changes when detected
struct ChangeEvent event = {...};
sql_tracker_log_change(region_id, &event);

// 4. Query stored data
sql_tracker_query("SELECT * FROM memory_changes WHERE region_id = ?");

// See include/sql_tracker.h for full API
*/
