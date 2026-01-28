/**
 * SQL Tracker Example - JavaScript/Node.js
 * 
 * Demonstrates universal SQL variable tracking in JavaScript.
 * Tracks which database columns are modified by SQL operations.
 */

const {
    SQLTracker,
    SQLOperation,
    init,
    getInstance,
    trackQuery
} = require('./bindings/sql_tracker.js');

async function main() {
    console.log("=== SQL Tracker Example - JavaScript ===\n");
    
    // Initialize tracker
    const tracker = new SQLTracker('/tmp/sql_changes.jsonl');
    
    // Example 1: Track INSERT
    console.log("Tracking INSERT query...");
    let inserted = tracker.trackQuery(
        "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
        1,
        "mydb"
    );
    console.log(`Tracked ${inserted} column changes\n`);
    
    // Example 2: Track UPDATE
    console.log("Tracking UPDATE query...");
    let updated = tracker.trackQuery(
        "UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
        1,
        "mydb",
        "alice@example.com",
        "newemail@example.com"
    );
    console.log(`Tracked ${updated} column changes\n`);
    
    // Example 3: Track DELETE
    console.log("Tracking DELETE query...");
    let deleted = tracker.trackQuery(
        "DELETE FROM users WHERE id = 1",
        1,
        "mydb"
    );
    console.log(`Tracked ${deleted} column changes\n`);
    
    // Example 4: Track SELECT
    console.log("Tracking SELECT query...");
    let selected = tracker.trackQuery(
        "SELECT name, email FROM users WHERE age > 25",
        5,
        "mydb"
    );
    console.log(`Tracked ${selected} column changes\n`);
    
    // Example 5: Monitor sensitive fields
    console.log("=== Monitoring Sensitive Fields ===");
    monitorSensitiveOperations(tracker);
    console.log();
    
    // Example 6: Get changes by filter
    console.log("=== Changes to 'users' table ===");
    const userChanges = tracker.getChanges("users", null, null);
    for (const change of userChanges) {
        console.log(`- ${change.table_name}.${change.column_name} ` +
                   `[${change.operation}] (rows: ${change.rows_affected})`);
    }
    console.log();
    
    // Example 7: Get summary
    console.log("=== Summary Statistics ===");
    const summary = tracker.summary();
    console.log(`Total changes: ${summary.total_changes}`);
    console.log(`Operations: ${JSON.stringify(summary.operations)}`);
    console.log(`Tables: ${JSON.stringify(summary.tables)}`);
    console.log();
    
    // Example 8: Using global tracker
    console.log("=== Using Global Tracker ===");
    const globalTracker = getInstance();
    globalTracker.trackQuery("INSERT INTO logs (message) VALUES ('App started')", 1);
    console.log("Changes via global instance: " + globalTracker.allChanges().length);
    
    // Cleanup
    tracker.close();
    console.log("\nTracker closed.");
}

/**
 * Monitor sensitive field modifications
 */
function monitorSensitiveOperations(tracker) {
    const sensitiveFields = [
        'password',
        'credit_card',
        'ssn',
        'api_key',
        'secret'
    ];
    
    const queries = [
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

// Run example
main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});

/*
 * Run with:
 *   node examples/sql_tracker_example_javascript.js
 * 
 * Install dependencies:
 *   npm install ffi-napi ref-napi
 * 
 * Or use with a bundler like webpack for browser compatibility
 */
