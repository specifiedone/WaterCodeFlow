/**
 * Universal SQL Tracker for JavaScript/Node.js
 * Track SQL column-level changes across all databases
 */

const ffi = require('ffi-napi');
const ref = require('ref-napi');

// SQL operation constants
const SQLOperation = {
    UNKNOWN: 0,
    INSERT: 1,
    UPDATE: 2,
    DELETE: 3,
    SELECT: 4
};

// Reverse mapping for operation codes
const OpNames = {
    0: 'UNKNOWN',
    1: 'INSERT',
    2: 'UPDATE',
    3: 'DELETE',
    4: 'SELECT'
};

/**
 * SQL Change object
 */
class SQLChange {
    constructor(timestamp_ns, table_name, column_name, operation,
                old_value, new_value, rows_affected, database, full_query) {
        this.timestamp_ns = timestamp_ns;
        this.table_name = table_name;
        this.column_name = column_name;
        this.operation = operation;
        this.old_value = old_value;
        this.new_value = new_value;
        this.rows_affected = rows_affected;
        this.database = database;
        this.full_query = full_query;
    }
    
    toObject() {
        return {
            timestamp_ns: this.timestamp_ns,
            table_name: this.table_name,
            column_name: this.column_name,
            operation: OpNames[this.operation],
            old_value: this.old_value,
            new_value: this.new_value,
            rows_affected: this.rows_affected,
            database: this.database,
            full_query: this.full_query
        };
    }
}

/**
 * SQL Tracker class
 */
class SQLTracker {
    constructor(storage_path = null) {
        this.storage_path = storage_path;
        this.changes = [];
        
        try {
            // Load native library
            this.lib = ffi.Library('sql_tracker', {
                'sql_tracker_init': ['pointer', ['string']],
                'sql_tracker_track_query': ['int', [
                    'pointer',  // tracker
                    'string',   // query
                    'int',      // rows_affected
                    'string',   // database
                    'string',   // old_value
                    'string'    // new_value
                ]],
                'sql_tracker_free': ['void', ['pointer']]
            });
            
            // Initialize native tracker
            this.tracker = this.lib.sql_tracker_init(storage_path);
        } catch (e) {
            console.warn('Warning: Native sql_tracker library not found. Using fallback.');
            this.lib = null;
            this.tracker = null;
        }
    }
    
    /**
     * Track a SQL query
     */
    trackQuery(query, rowsAffected = 0, database = null, oldValue = null, newValue = null) {
        if (this.lib && this.tracker) {
            return this.lib.sql_tracker_track_query(
                this.tracker,
                query,
                rowsAffected,
                database || '',
                oldValue || '',
                newValue || ''
            );
        }
        return 0;
    }
    
    /**
     * Get changes with optional filters
     */
    getChanges(tableFilter = null, columnFilter = null, operationFilter = null) {
        return this.changes.filter(change => {
            if (tableFilter && change.table_name !== tableFilter)
                return false;
            if (columnFilter && change.column_name !== columnFilter)
                return false;
            if (operationFilter && OpNames[change.operation] !== operationFilter)
                return false;
            return true;
        });
    }
    
    /**
     * Get all changes
     */
    allChanges() {
        return [...this.changes];
    }
    
    /**
     * Get summary statistics
     */
    summary() {
        const summary = {
            total_changes: this.changes.length,
            insert_count: 0,
            update_count: 0,
            delete_count: 0,
            select_count: 0,
            tables: {},
            columns: new Set()
        };
        
        for (const change of this.changes) {
            switch (change.operation) {
                case SQLOperation.INSERT:
                    summary.insert_count++;
                    break;
                case SQLOperation.UPDATE:
                    summary.update_count++;
                    break;
                case SQLOperation.DELETE:
                    summary.delete_count++;
                    break;
                case SQLOperation.SELECT:
                    summary.select_count++;
                    break;
            }
            
            summary.tables[change.table_name] = (summary.tables[change.table_name] || 0) + 1;
            summary.columns.add(`${change.table_name}.${change.column_name}`);
        }
        
        return {
            total_changes: summary.total_changes,
            operations: {
                INSERT: summary.insert_count,
                UPDATE: summary.update_count,
                DELETE: summary.delete_count,
                SELECT: summary.select_count
            },
            tables: summary.tables,
            columns: Array.from(summary.columns)
        };
    }
    
    /**
     * Cleanup
     */
    close() {
        if (this.lib && this.tracker) {
            this.lib.sql_tracker_free(this.tracker);
            this.tracker = null;
        }
    }
    
    /**
     * Destructor
     */
    [Symbol.dispose]() {
        this.close();
    }
}

// Global tracker instance
let globalTracker = null;

/**
 * Initialize global tracker
 */
function init(storagePath = null) {
    if (globalTracker) {
        globalTracker.close();
    }
    globalTracker = new SQLTracker(storagePath);
    return globalTracker;
}

/**
 * Get global tracker instance
 */
function getInstance() {
    if (!globalTracker) {
        globalTracker = new SQLTracker();
    }
    return globalTracker;
}

/**
 * Track query using global tracker
 */
function trackQuery(query, rowsAffected = 0, database = null, oldValue = null, newValue = null) {
    return getInstance().trackQuery(query, rowsAffected, database, oldValue, newValue);
}

// Export module
module.exports = {
    SQLTracker,
    SQLChange,
    SQLOperation,
    init,
    getInstance,
    trackQuery
};

// Example usage (uncomment to test):
/*
const { SQLTracker } = require('./sql_tracker.js');

const tracker = new SQLTracker('/tmp/sql_changes.jsonl');

tracker.trackQuery(
    "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')",
    1,
    'mydb'
);

tracker.trackQuery(
    "UPDATE users SET email = 'new@example.com' WHERE id = 1",
    1,
    'mydb',
    'old@example.com',
    'new@example.com'
);

console.log(tracker.summary());

tracker.close();
*/
