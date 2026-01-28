/*
 * memwatch_tracker.h - Memory tracking API
 */

#ifndef MEMWATCH_TRACKER_H
#define MEMWATCH_TRACKER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Initialize memory tracking system
 * 
 * @param db_path Path to SQLite database for storing events
 * @param track_all Whether to track all variables
 * @param track_sql Whether to track SQL operations
 * @param track_threads Whether to record thread IDs
 * @param scope Scope filter: "global", "local", or "both"
 * @return 0 on success, -1 on failure
 */
int tracker_init(const char *db_path, bool track_all, bool track_sql, bool track_threads, const char *scope);

/**
 * Start tracking a memory region
 * 
 * Protects the region with mprotect(PROT_NONE) and installs SIGSEGV handler.
 * When writes occur, changes are recorded to the database.
 * 
 * @param address Memory address to start tracking
 * @param size Size of region in bytes
 * @param name Human-readable name (e.g., "my_buffer")
 * @return Region ID on success, -1 on failure
 */
int tracker_watch(uint64_t address, size_t size, const char *name);

/**
 * Stop tracking a memory region
 * 
 * Restores the region to PROT_READ | PROT_WRITE.
 * 
 * @param region_id Region ID returned by tracker_watch()
 * @return 0 on success, -1 on failure
 */
int tracker_unwatch(int region_id);

/**
 * Flush pending events and close tracker
 * 
 * Must be called before program exit to ensure all events are saved.
 */
void tracker_close(void);

/**
 * Get number of events recorded so far
 */
int tracker_get_event_count(void);

/**
 * Log a SQL query for tracking
 * 
 * @param query_text The SQL query string to log
 */
void tracker_log_sql_query(const char *query_text);

/**
 * Increment execution step counter
 * Used to track execution progress for variable changes
 */
void tracker_step(void);

/**
 * Update current execution context (file, function, line)
 * 
 * @param filename Source file name
 * @param funcname Function name
 * @param line_num Line number in source
 */
void tracker_set_context(const char *filename, const char *funcname, int line_num);

/**
 * Log function entry for execution tracking
 */
void tracker_func_enter(const char *func_name, const char *file_name);

/**
 * Log function exit for execution tracking
 */
void tracker_func_exit(const char *func_name);

#endif
