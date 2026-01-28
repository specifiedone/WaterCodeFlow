/**
 * SQL Tracker Example - TypeScript
 * 
 * Demonstrates universal SQL variable tracking in TypeScript.
 * Full type safety with TypeScript interfaces.
 */

import {
    SQLTracker,
    SQLChange,
    SQLOperation,
    init,
    getInstance,
    trackQuery,
    ISummary
} from './bindings/sql_tracker.ts';

async function main(): Promise<void> {
    console.log("=== SQL Tracker Example - TypeScript ===\n");
    
    // Initialize tracker
    const tracker = new SQLTracker('/tmp/sql_changes.jsonl');
    
    // Example 1: Track INSERT
    console.log("Tracking INSERT query...");
    const inserted: number = tracker.trackQuery(
        "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
        1,
        "mydb"
    );
    console.log(`Tracked ${inserted} column changes\n`);
    
    // Example 2: Track UPDATE
    console.log("Tracking UPDATE query...");
    const updated: number = tracker.trackQuery(
        "UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
        1,
        "mydb",
        "alice@example.com",
        "newemail@example.com"
    );
    console.log(`Tracked ${updated} column changes\n`);
    
    // Example 3: Track DELETE
    console.log("Tracking DELETE query...");
    const deleted: number = tracker.trackQuery(
        "DELETE FROM users WHERE id = 1",
        1,
        "mydb"
    );
    console.log(`Tracked ${deleted} column changes\n`);
    
    // Example 4: Track SELECT
    console.log("Tracking SELECT query...");
    const selected: number = tracker.trackQuery(
        "SELECT name, email FROM users WHERE age > 25",
        5,
        "mydb"
    );
    console.log(`Tracked ${selected} column changes\n`);
    
    // Example 5: Monitor sensitive fields with type safety
    console.log("=== Monitoring Sensitive Fields ===");
    monitorSensitiveOperations(tracker);
    console.log();
    
    // Example 6: Get changes with type safety
    console.log("=== Changes to 'users' table ===");
    const userChanges: SQLChange[] = tracker.getChanges("users");
    for (const change of userChanges) {
        console.log(
            `- ${change.table_name}.${change.column_name} ` +
            `[${SQLOperation[change.operation]}] ` +
            `(rows: ${change.rows_affected})`
        );
    }
    console.log();
    
    // Example 7: Get summary with type safety
    console.log("=== Summary Statistics ===");
    const summary: ISummary = tracker.getSummary();
    console.log(`Total changes: ${summary.total_changes}`);
    console.log(`INSERT: ${summary.insert_count}, UPDATE: ${summary.update_count}, ` +
                `DELETE: ${summary.delete_count}, SELECT: ${summary.select_count}`);
    console.log(`Tables: ${Object.keys(summary.tables).join(', ')}`);
    console.log(`Columns: ${summary.columns.join(', ')}`);
    console.log();
    
    // Example 8: Using global tracker
    console.log("=== Using Global Tracker ===");
    const globalTracker: SQLTracker = getInstance();
    globalTracker.trackQuery("INSERT INTO logs (message) VALUES ('App started')", 1, "mydb");
    console.log(`Changes via global instance: ${globalTracker.getAllChanges().length}`);
    
    // Cleanup
    tracker.close();
    console.log("\nTracker closed.");
}

/**
 * Monitor sensitive field modifications with type safety
 */
function monitorSensitiveOperations(tracker: SQLTracker): void {
    const sensitiveFields: string[] = [
        'password',
        'credit_card',
        'ssn',
        'api_key',
        'secret'
    ];
    
    const queries: string[] = [
        "UPDATE users SET password = 'hashed_pw' WHERE id = 1",
        "INSERT INTO payment (user_id, credit_card) VALUES (1, '4111-1111-1111-1111')",
        "SELECT ssn FROM employees WHERE department = 'HR'",
        "UPDATE settings SET api_key = 'sk_live_abc' WHERE user_id = 5"
    ];
    
    for (const query of queries) {
        tracker.trackQuery(query, 1, "mydb");
        
        // Check for sensitive operations
        for (const field of sensitiveFields) {
            if (query.toLowerCase().includes(field)) {
                console.log(`🚨 ALERT: Sensitive field '${field}' accessed!`);
                console.log(`   Query: ${query}`);
            }
        }
    }
}

/**
 * Example of type-safe change processing
 */
function processChanges(tracker: SQLTracker): void {
    const changes: SQLChange[] = tracker.getAllChanges();
    
    for (const change of changes) {
        // TypeScript ensures type safety
        const operationName: string = SQLOperation[change.operation];
        
        // Process based on operation type
        switch (change.operation) {
            case SQLOperation.INSERT:
                console.log(`New record inserted into ${change.table_name}`);
                break;
            case SQLOperation.UPDATE:
                console.log(`Column ${change.column_name} updated in ${change.table_name}`);
                break;
            case SQLOperation.DELETE:
                console.log(`Record deleted from ${change.table_name}`);
                break;
            case SQLOperation.SELECT:
                console.log(`Data selected from ${change.table_name}`);
                break;
        }
    }
}

// Run example
main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});

/*
 * Compile with:
 *   tsc examples/sql_tracker_example_typescript.ts
 * 
 * Run with:
 *   node examples/sql_tracker_example_typescript.js
 * 
 * In a TypeScript project:
 *   ts-node examples/sql_tracker_example_typescript.ts
 * 
 * Benefits of TypeScript:
 *   - Type-safe SQL tracker usage
 *   - IntelliSense support
 *   - Compile-time error checking
 *   - Self-documenting code
 */
