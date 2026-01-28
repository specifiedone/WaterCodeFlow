/**
 * Universal SQL Tracker for TypeScript
 * Track SQL column-level changes across all databases
 */

enum SQLOperation {
    UNKNOWN = 0,
    INSERT = 1,
    UPDATE = 2,
    DELETE = 3,
    SELECT = 4
}

interface ISQLChange {
    timestamp_ns: number;
    table_name: string;
    column_name: string;
    operation: SQLOperation;
    old_value?: string;
    new_value?: string;
    rows_affected: number;
    database?: string;
    full_query: string;
}

interface ISummary {
    total_changes: number;
    insert_count: number;
    update_count: number;
    delete_count: number;
    select_count: number;
    tables: Record<string, number>;
    columns: string[];
}

/**
 * SQL Change object
 */
class SQLChange implements ISQLChange {
    timestamp_ns: number;
    table_name: string;
    column_name: string;
    operation: SQLOperation;
    old_value?: string;
    new_value?: string;
    rows_affected: number;
    database?: string;
    full_query: string;
    
    constructor(
        timestamp_ns: number,
        table_name: string,
        column_name: string,
        operation: SQLOperation,
        old_value?: string,
        new_value?: string,
        rows_affected: number = 0,
        database?: string,
        full_query: string = ""
    ) {
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
    
    toJSON(): object {
        return {
            timestamp_ns: this.timestamp_ns,
            table_name: this.table_name,
            column_name: this.column_name,
            operation: SQLOperation[this.operation],
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
    private storagePath: string | null;
    private changes: SQLChange[] = [];
    private native: any = null;
    
    constructor(storagePath?: string) {
        this.storagePath = storagePath || null;
        this.initNativeLibrary();
    }
    
    private initNativeLibrary(): void {
        try {
            // In a real TypeScript implementation, you would load the native module
            // const Module = require('../build/Release/sql_tracker');
            // this.native = Module.init(this.storagePath);
        } catch (error) {
            console.warn('Warning: Native sql_tracker library not found.');
        }
    }
    
    /**
     * Track a SQL query
     */
    trackQuery(
        query: string,
        rowsAffected: number = 0,
        database?: string,
        oldValue?: string,
        newValue?: string
    ): number {
        if (this.native) {
            return this.native.trackQuery(query, rowsAffected, database, oldValue, newValue);
        }
        return 0;
    }
    
    /**
     * Get changes with optional filters
     */
    getChanges(
        tableFilter?: string,
        columnFilter?: string,
        operationFilter?: string
    ): SQLChange[] {
        return this.changes.filter(change => {
            if (tableFilter && change.table_name !== tableFilter) return false;
            if (columnFilter && change.column_name !== columnFilter) return false;
            if (operationFilter && SQLOperation[change.operation] !== operationFilter) return false;
            return true;
        });
    }
    
    /**
     * Get all changes
     */
    getAllChanges(): SQLChange[] {
        return [...this.changes];
    }
    
    /**
     * Get summary statistics
     */
    getSummary(): ISummary {
        const summary: ISummary = {
            total_changes: this.changes.length,
            insert_count: 0,
            update_count: 0,
            delete_count: 0,
            select_count: 0,
            tables: {},
            columns: []
        };
        
        const columnSet = new Set<string>();
        
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
            columnSet.add(`${change.table_name}.${change.column_name}`);
        }
        
        summary.columns = Array.from(columnSet);
        return summary;
    }
    
    /**
     * Close tracker
     */
    close(): void {
        if (this.native) {
            this.native.close();
        }
    }
    
    /**
     * Destructor
     */
    [Symbol.dispose](): void {
        this.close();
    }
}

// Global tracker instance
let globalTracker: SQLTracker | null = null;

/**
 * Initialize global tracker
 */
export function init(storagePath?: string): SQLTracker {
    if (globalTracker) {
        globalTracker.close();
    }
    globalTracker = new SQLTracker(storagePath);
    return globalTracker;
}

/**
 * Get global tracker instance
 */
export function getInstance(): SQLTracker {
    if (!globalTracker) {
        globalTracker = new SQLTracker();
    }
    return globalTracker;
}

/**
 * Track query using global tracker
 */
export function trackQuery(
    query: string,
    rowsAffected: number = 0,
    database?: string,
    oldValue?: string,
    newValue?: string
): number {
    return getInstance().trackQuery(query, rowsAffected, database, oldValue, newValue);
}

// Exports
export {
    SQLTracker,
    SQLChange,
    SQLOperation,
    ISQLChange,
    ISummary
};

// Example usage:
/*
import { SQLTracker, SQLOperation } from './sql_tracker';

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

console.log(tracker.getSummary());
tracker.close();
*/
