/**
 * SQL Tracker Example - Rust
 * 
 * Demonstrates universal SQL variable tracking in Rust.
 * Tracks which database columns are modified by SQL operations.
 */

use std::collections::HashMap;

// Include the sql_tracker module
mod sqltracker;
use sqltracker::{SQLTracker, SQLOperation};

fn main() {
    println!("=== SQL Tracker Example - Rust ===\n");
    
    // Initialize tracker
    let mut tracker = SQLTracker::new(Some("/tmp/sql_changes.jsonl"));
    
    // Example 1: Track INSERT
    println!("Tracking INSERT query...");
    let inserted = tracker.track_query(
        "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
        1,
        Some("mydb"),
        None,
        None,
    );
    println!("Tracked {} column changes\n", inserted);
    
    // Example 2: Track UPDATE
    println!("Tracking UPDATE query...");
    let updated = tracker.track_query(
        "UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
        1,
        Some("mydb"),
        Some("alice@example.com"),
        Some("newemail@example.com"),
    );
    println!("Tracked {} column changes\n", updated);
    
    // Example 3: Track DELETE
    println!("Tracking DELETE query...");
    let deleted = tracker.track_query(
        "DELETE FROM users WHERE id = 1",
        1,
        Some("mydb"),
        None,
        None,
    );
    println!("Tracked {} column changes\n", deleted);
    
    // Example 4: Track SELECT
    println!("Tracking SELECT query...");
    let selected = tracker.track_query(
        "SELECT name, email FROM users WHERE age > 25",
        5,
        Some("mydb"),
        None,
        None,
    );
    println!("Tracked {} column changes\n", selected);
    
    // Example 5: Monitor sensitive fields
    println!("=== Monitoring Sensitive Fields ===");
    monitor_sensitive_operations(&mut tracker);
    println!();
    
    // Example 6: Get changes by filter
    println!("=== Changes to 'users' table ===");
    let user_changes = tracker.get_changes(Some("users"), None, None);
    for change in user_changes {
        println!(
            "- {}.{} [{}] (rows: {})",
            change.table_name,
            change.column_name,
            change.operation.as_str(),
            change.rows_affected
        );
    }
    println!();
    
    // Example 7: Get summary
    println!("=== Summary Statistics ===");
    let summary = tracker.summary();
    println!("Total changes: {}", summary.total_changes);
    println!(
        "INSERT: {}, UPDATE: {}, DELETE: {}, SELECT: {}",
        summary.insert_count, summary.update_count, summary.delete_count, summary.select_count
    );
    println!("Tables: {:?}", summary.tables);
    println!("Columns: {:?}", summary.columns);
}

/**
 * Monitor sensitive field modifications
 */
fn monitor_sensitive_operations(tracker: &mut SQLTracker) {
    let sensitive_fields = vec!["password", "credit_card", "ssn", "api_key", "secret"];
    
    let queries = vec![
        "UPDATE users SET password = 'hashed_pw' WHERE id = 1",
        "INSERT INTO payment (user_id, credit_card) VALUES (1, '4111-1111-1111-1111')",
        "SELECT ssn FROM employees WHERE department = 'HR'",
    ];
    
    for query in queries {
        tracker.track_query(query, 1, Some("mydb"), None, None);
        
        // Check for sensitive operations
        for field in &sensitive_fields {
            if query.to_lowercase().contains(field) {
                println!("🚨 ALERT: Sensitive field '{}' accessed!", field);
                println!("   Query: {}", query);
            }
        }
    }
}

/*
 * Build with:
 *   rustc examples/sql_tracker_example_rust.rs -L target/release/deps
 * 
 * Or in a Cargo project:
 *   [dependencies]
 *   libc = "0.2"
 * 
 *   cargo build --example sql_tracker_example_rust
 *   cargo run --example sql_tracker_example_rust
 */
