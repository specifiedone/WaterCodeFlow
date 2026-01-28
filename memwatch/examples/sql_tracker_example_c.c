/**
 * SQL Tracker Example - C
 * 
 * Demonstrates universal SQL variable tracking in C.
 * Tracks which database columns are modified by INSERT, UPDATE, DELETE, SELECT operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../include/sql_tracker.h"

int main(int argc, char *argv[]) {
    printf("=== SQL Tracker Example - C ===\n\n");
    
    // Initialize tracker
    SQLTracker *tracker = sql_tracker_init(NULL);  // Or "/tmp/sql_changes.jsonl" for persistence
    if (!tracker) {
        fprintf(stderr, "Failed to initialize tracker\n");
        return 1;
    }
    
    // Example 1: Track INSERT operation
    printf("Tracking INSERT query...\n");
    int inserted = sql_tracker_track_query(
        tracker,
        "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
        1,
        "mydb",
        NULL,
        NULL
    );
    printf("Tracked %d column changes\n\n", inserted);
    
    // Example 2: Track UPDATE operation
    printf("Tracking UPDATE query...\n");
    int updated = sql_tracker_track_query(
        tracker,
        "UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
        1,
        "mydb",
        "alice@example.com",
        "newemail@example.com"
    );
    printf("Tracked %d column changes\n\n", updated);
    
    // Example 3: Track DELETE operation
    printf("Tracking DELETE query...\n");
    int deleted = sql_tracker_track_query(
        tracker,
        "DELETE FROM users WHERE id = 1",
        1,
        "mydb",
        NULL,
        NULL
    );
    printf("Tracked %d column changes\n\n", deleted);
    
    // Example 4: Track SELECT operation
    printf("Tracking SELECT query...\n");
    int selected = sql_tracker_track_query(
        tracker,
        "SELECT name, email FROM users WHERE age > 25",
        5,
        "mydb",
        NULL,
        NULL
    );
    printf("Tracked %d column changes\n\n", selected);
    
    // Get summary statistics
    SQLTrackerSummary summary;
    sql_tracker_summary(tracker, &summary);
    
    printf("=== Summary Statistics ===\n");
    printf("Total changes: %d\n", summary.total_changes);
    printf("INSERT operations: %d\n", summary.insert_count);
    printf("UPDATE operations: %d\n", summary.update_count);
    printf("DELETE operations: %d\n", summary.delete_count);
    printf("SELECT operations: %d\n", summary.select_count);
    printf("\n");
    
    // Get changes with filters
    SQLChange *changes = NULL;
    int count = sql_tracker_get_changes(tracker, "users", NULL, NULL, &changes);
    
    printf("=== Changes to 'users' table ===\n");
    for (int i = 0; i < count && i < 10; i++) {
        printf("- %s: %s [%s] (rows affected: %d)\n",
               changes[i].table_name,
               changes[i].column_name,
               sql_operation_to_string(changes[i].operation),
               changes[i].rows_affected);
    }
    
    if (changes) {
        free(changes);
    }
    
    // Cleanup
    sql_tracker_free(tracker);
    printf("\nTracker freed.\n");
    
    return 0;
}

/*
 * Compile with:
 *   gcc -o sql_tracker_example_c examples/sql_tracker_example_c.c src/sql_tracker.c -I include -lm
 * 
 * Run with:
 *   ./sql_tracker_example_c
 */
