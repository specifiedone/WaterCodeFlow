/*
 * memwatch_extended.h - Extended API with threading, scoping, and cross-language support
 *
 * Adds advanced features:
 * - Per-thread variable tracking
 * - Local vs global scope selection
 * - Cross-language variable tracking (SQL in Python, etc.)
 * - Persistent change storage
 */

#ifndef MEMWATCH_EXTENDED_H
#define MEMWATCH_EXTENDED_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Extended Types
 * ============================================================================ */

typedef uint32_t memwatch_thread_id;
typedef uint32_t memwatch_variable_id;

/* Variable scope */
typedef enum {
    MEMWATCH_SCOPE_GLOBAL = 0,
    MEMWATCH_SCOPE_LOCAL = 1,
    MEMWATCH_SCOPE_BOTH = 2,
} memwatch_scope_t;

/* Variable source language */
typedef enum {
    MEMWATCH_LANG_PYTHON = 0,
    MEMWATCH_LANG_C = 1,
    MEMWATCH_LANG_JAVASCRIPT = 2,
    MEMWATCH_LANG_JAVA = 3,
    MEMWATCH_LANG_GO = 4,
    MEMWATCH_LANG_RUST = 5,
    MEMWATCH_LANG_CSHARP = 6,
    MEMWATCH_LANG_SQL = 7,
    MEMWATCH_LANG_UNKNOWN = 255,
} memwatch_language_t;

/* Extended change event with threading and scope info */
typedef struct {
    uint32_t seq;
    uint64_t timestamp_ns;
    uint32_t adapter_id;
    uint32_t region_id;
    uint32_t variable_id;
    
    /* Threading info */
    uint32_t thread_id;
    const char *thread_name;
    
    /* Scope info */
    memwatch_scope_t scope;
    memwatch_language_t language;
    
    /* Variable metadata */
    const char *variable_name;
    const char *file;
    const char *function;
    uint32_t line;
    uint64_t fault_ip;
    
    /* Values */
    const uint8_t *old_preview;
    size_t old_preview_size;
    
    const uint8_t *new_preview;
    size_t new_preview_size;
    
    const uint8_t *old_value;
    size_t old_value_size;
    
    const uint8_t *new_value;
    size_t new_value_size;
    
    const char *storage_key_old;
    const char *storage_key_new;
    
    /* Stack trace for this thread */
    const char **stack_trace;
    size_t stack_trace_depth;
    
    void *user_data;
} memwatch_extended_event_t;

/* Extended callback */
typedef void (*memwatch_extended_callback_t)(
    const memwatch_extended_event_t *event, 
    void *user_ctx
);

/* ============================================================================
 * Extended API
 * ============================================================================ */

/**
 * Watch a variable with extended metadata
 * 
 * Args:
 *   addr: Memory address
 *   size: Size in bytes
 *   name: Variable name
 *   scope: Local, global, or both
 *   language: Source language
 *   thread_id: Current thread ID (0 for auto)
 *   thread_name: Human-readable thread name (optional)
 *   user_data: Custom metadata
 * 
 * Returns: variable_id on success, 0 on error
 */
uint32_t memwatch_watch_extended(
    uint64_t addr,
    size_t size,
    const char *name,
    memwatch_scope_t scope,
    memwatch_language_t language,
    uint32_t thread_id,
    const char *thread_name,
    void *user_data
);

/**
 * Set extended callback with threading info
 */
int memwatch_set_extended_callback(
    memwatch_extended_callback_t callback,
    void *user_ctx
);

/**
 * Get changes for specific thread
 */
int memwatch_check_changes_for_thread(
    uint32_t thread_id,
    memwatch_extended_event_t *out_events,
    int max_events
);

/**
 * Get all variables being tracked
 */
typedef struct {
    uint32_t variable_id;
    const char *name;
    uint32_t thread_id;
    const char *thread_name;
    memwatch_scope_t scope;
    memwatch_language_t language;
    uint64_t addr;
    size_t size;
    uint64_t last_change_time;
    uint32_t change_count;
} memwatch_variable_info_t;

int memwatch_get_variables(
    memwatch_variable_info_t *out_vars,
    int max_vars
);

/**
 * Auto-watch all variables in scope
 * 
 * For dynamic languages - automatically track all accessible variables
 */
int memwatch_watch_all_in_scope(
    memwatch_scope_t scope,
    memwatch_language_t language
);

/**
 * Cross-language variable tracking
 * 
 * Track a variable from another language context
 * E.g., SQL columns when used in Python
 */
int memwatch_watch_foreign_variable(
    uint64_t addr,
    size_t size,
    const char *name,
    memwatch_language_t from_language,
    memwatch_language_t to_language,
    const char *context,  /* e.g., "SQL.users.email" */
    void *user_data
);

/**
 * Persistent storage functions
 */

/**
 * Start recording changes to persistent storage
 */
int memwatch_storage_init(const char *storage_path);

/**
 * Stop recording and flush to storage
 */
int memwatch_storage_flush(void);

/**
 * Query stored changes
 */
typedef struct {
    uint32_t seq;
    uint64_t timestamp_ns;
    uint32_t thread_id;
    const char *thread_name;
    const char *variable_name;
    memwatch_language_t language;
    memwatch_scope_t scope;
    const char *old_preview;
    const char *new_preview;
    const char *file;
    const char *function;
    uint32_t line;
} memwatch_stored_change_t;

int memwatch_storage_query(
    const char *variable_filter,  /* NULL for all */
    uint32_t thread_id,           /* 0 for all threads */
    memwatch_stored_change_t *out_changes,
    int max_changes
);

/**
 * Statistics including threading info
 */
typedef struct {
    uint32_t num_tracked_regions;
    uint32_t num_active_threads;
    uint32_t num_variables_global;
    uint32_t num_variables_local;
    uint64_t total_events;
    uint64_t events_by_thread[256];  /* indexed by thread_id % 256 */
    uint64_t storage_bytes_used;
} memwatch_extended_stats_t;

int memwatch_get_extended_stats(memwatch_extended_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* MEMWATCH_EXTENDED_H */
