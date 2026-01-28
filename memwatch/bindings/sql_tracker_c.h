/**
 * SQL Tracker C Binding - Direct use of sql_tracker.h
 * C applications just include sql_tracker.h and link against libsql_tracker
 */

#include "../include/sql_tracker.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Example usage in C:
 * 
 *   #include "sql_tracker.h"
 *   
 *   int main() {
 *       SQLTracker *tracker = sql_tracker_init(NULL);
 *       
 *       sql_tracker_track_query(tracker, "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')", 1, NULL, NULL, NULL);
 *       
 *       SQLTrackerSummary summary;
 *       sql_tracker_summary(tracker, &summary);
 *       printf("Total changes: %d\n", summary.total_changes);
 *       
 *       sql_tracker_free(tracker);
 *       return 0;
 *   }
 * 
 * Compile with:
 *   gcc -o my_program my_program.c src/sql_tracker.c -I include -lm
 */

// No additional binding needed - C uses the header directly
