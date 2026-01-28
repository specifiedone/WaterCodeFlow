/**
 * SQL Tracker - Universal SQL parsing and column change detection
 * Language-agnostic header for use across all 10 languages
 */

#ifndef SQL_TRACKER_H
#define SQL_TRACKER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TABLE_NAME 256
#define MAX_COLUMN_NAME 256
#define MAX_VALUE_LENGTH 1024
#define MAX_QUERY_LENGTH 4096
#define MAX_DATABASE_NAME 256
#define MAX_CHANGES 10000

/**
 * SQL operation types
 */
typedef enum {
    SQL_UNKNOWN = 0,
    SQL_INSERT = 1,
    SQL_UPDATE = 2,
    SQL_DELETE = 3,
    SQL_SELECT = 4
} SQLOperation;

/**
 * Single column change from SQL operation
 */
typedef struct {
    uint64_t timestamp_ns;                  // Nanosecond timestamp
    char table_name[MAX_TABLE_NAME];        // Table affected
    char column_name[MAX_COLUMN_NAME];      // Column affected
    SQLOperation operation;                 // INSERT, UPDATE, DELETE, SELECT
    char old_value[MAX_VALUE_LENGTH];       // Previous value (optional)
    char new_value[MAX_VALUE_LENGTH];       // New value (optional)
    int rows_affected;                      // Number of rows affected
    char database[MAX_DATABASE_NAME];       // Database name (optional)
    char full_query[MAX_QUERY_LENGTH];      // Complete SQL query
} SQLChange;

/**
 * SQL Tracker instance
 */
typedef struct {
    SQLChange *changes;                     // Array of changes
    int change_count;                       // Current number of changes
    int max_changes;                        // Maximum capacity
    char *storage_path;                     // Optional file path for persistence
} SQLTracker;

/**
 * Summary statistics
 */
typedef struct {
    int total_changes;
    int insert_count;
    int update_count;
    int delete_count;
    int select_count;
} SQLTrackerSummary;

/**
 * Initialize SQL tracker
 * 
 * Args:
 *   storage_path - Optional file path to persist changes (JSONL format)
 * 
 * Returns:
 *   Pointer to SQLTracker, or NULL on error
 */
SQLTracker *sql_tracker_init(const char *storage_path);

/**
 * Track a SQL query and extract column changes
 * 
 * Args:
 *   tracker - SQLTracker instance
 *   query - SQL query string
 *   rows_affected - Number of rows affected by operation
 *   database - Optional database name
 *   old_value - Optional old value for update operations
 *   new_value - Optional new value for insert/update operations
 * 
 * Returns:
 *   Number of column changes created (0 if query could not be parsed)
 */
int sql_tracker_track_query(SQLTracker *tracker, const char *query, int rows_affected,
                            const char *database, const char *old_value, const char *new_value);

/**
 * Get global tracker instance
 * 
 * Returns:
 *   Pointer to global SQLTracker, or NULL if not initialized
 */
SQLTracker *sql_tracker_get_global(void);

/**
 * Get summary statistics
 * 
 * Args:
 *   tracker - SQLTracker instance
 *   summary - Pointer to SQLTrackerSummary struct to fill
 */
void sql_tracker_summary(SQLTracker *tracker, SQLTrackerSummary *summary);

/**
 * Get filtered changes
 * 
 * Args:
 *   tracker - SQLTracker instance
 *   table_filter - Optional table name filter
 *   column_filter - Optional column name filter
 *   operation_filter - Optional operation filter ("INSERT", "UPDATE", "DELETE", "SELECT")
 *   out_changes - Pointer to receive array of matching changes
 * 
 * Returns:
 *   Number of matching changes
 * 
 * Note:
 *   Caller must free out_changes with free()
 */
int sql_tracker_get_changes(SQLTracker *tracker, const char *table_filter,
                            const char *column_filter, const char *operation_filter,
                            SQLChange **out_changes);

/**
 * Free tracker and all allocated memory
 * 
 * Args:
 *   tracker - SQLTracker instance to free
 */
void sql_tracker_free(SQLTracker *tracker);

/**
 * Get operation type as string
 * 
 * Args:
 *   op - SQLOperation enum value
 * 
 * Returns:
 *   String representation ("INSERT", "UPDATE", "DELETE", "SELECT", or "UNKNOWN")
 */
static inline const char *sql_operation_to_string(SQLOperation op) {
    switch (op) {
        case SQL_INSERT: return "INSERT";
        case SQL_UPDATE: return "UPDATE";
        case SQL_DELETE: return "DELETE";
        case SQL_SELECT: return "SELECT";
        case SQL_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif

#endif // SQL_TRACKER_H
