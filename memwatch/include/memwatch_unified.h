/*
 * memwatch_unified.h - Unified C API for all language bindings
 * 
 * This header provides a single, consistent API that all language bindings
 * use internally. It's a thin wrapper over the existing memwatch.c implementation.
 */

#ifndef MEMWATCH_UNIFIED_H
#define MEMWATCH_UNIFIED_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Types
 * ============================================================================ */

typedef uint32_t memwatch_region_id;
typedef uint32_t memwatch_adapter_id;

/* Change event - same structure across all languages */
typedef struct {
    uint32_t seq;
    uint64_t timestamp_ns;
    uint32_t adapter_id;
    uint32_t region_id;
    const char *variable_name;
    const char *file;
    const char *function;
    uint32_t line;
    uint64_t fault_ip;
    
    /* Values - allocate and provide to callback */
    const uint8_t *old_preview;  /* First 256 bytes or less */
    size_t old_preview_size;
    
    const uint8_t *new_preview;  /* First 256 bytes or less */
    size_t new_preview_size;
    
    const uint8_t *old_value;    /* Full value if < 4KB, NULL if larger */
    size_t old_value_size;
    
    const uint8_t *new_value;    /* Full value if < 4KB, NULL if larger */
    size_t new_value_size;
    
    const char *storage_key_old;  /* For large values (> 4KB) */
    const char *storage_key_new;
    
    /* Custom metadata from watch() call */
    const void *user_data;
} memwatch_change_event_t;

/* Callback function signature - same for all languages */
typedef void (*memwatch_callback_t)(const memwatch_change_event_t *event, void *user_ctx);

/* ============================================================================
 * Core API - Unified for All Languages
 * ============================================================================ */

/**
 * Initialize memwatch
 * 
 * Must be called once before any watch() calls.
 * Safe to call multiple times (idempotent).
 * 
 * Returns: 0 on success, negative on error
 */
int memwatch_init(void);

/**
 * Shutdown memwatch and release all resources
 * 
 * Can be called at any time. After this, watch() will fail until
 * another init() is called.
 */
void memwatch_shutdown(void);

/**
 * Watch a memory region
 * 
 * Args:
 *   addr: Memory address to watch (must be page-aligned for mprotect)
 *   size: Size in bytes
 *   name: Human-readable variable name (optional, can be NULL)
 *   user_data: Arbitrary pointer passed to callback (optional, can be NULL)
 * 
 * Returns: region_id > 0 on success, 0 on error
 */
memwatch_region_id memwatch_watch(uint64_t addr, size_t size, 
                                  const char *name, void *user_data);

/**
 * Stop watching a region
 * 
 * Args:
 *   region_id: ID returned from watch()
 * 
 * Returns: true if found and untracked, false if not found
 */
bool memwatch_unwatch(memwatch_region_id region_id);

/**
 * Set global callback for all change events
 * 
 * The callback will be invoked (possibly from a signal handler context)
 * when tracked memory changes are detected.
 * 
 * Args:
 *   callback: Function to invoke on changes, or NULL to disable
 *   user_ctx: Context pointer passed to callback (can be NULL)
 * 
 * Returns: 0 on success, negative on error
 */
int memwatch_set_callback(memwatch_callback_t callback, void *user_ctx);

/**
 * Manually check for changes (polling mode)
 * 
 * For use when event-driven callbacks are not suitable.
 * Returns events that have accumulated since last call.
 * 
 * Args:
 *   out_events: Pointer to array to fill with events
 *   max_events: Maximum number of events to return
 * 
 * Returns: Number of events returned (0 to max_events)
 */
int memwatch_check_changes(memwatch_change_event_t *out_events, int max_events);

/**
 * Get current statistics
 * 
 * Args:
 *   out_stats: Pointer to stats structure to fill
 * 
 * Returns: 0 on success, negative on error
 */
typedef struct {
    uint32_t num_tracked_regions;
    uint32_t num_active_watchpoints;
    uint64_t total_events;
    uint64_t ring_write_count;
    uint64_t ring_drop_count;
    uint64_t storage_bytes_used;
    
    /* Platform-specific stats */
    uint32_t mprotect_page_count;  /* Linux/macOS only */
    uint32_t worker_thread_id;
    uint64_t worker_cycles;
} memwatch_stats_t;

int memwatch_get_stats(memwatch_stats_t *out_stats);

/**
 * Free event resources
 * 
 * Releases memory associated with an event (previews, values, etc.)
 * Safe to call multiple times (idempotent).
 */
void memwatch_free_event(memwatch_change_event_t *event);

/* ============================================================================
 * Adapter Management (for language bindings)
 * ============================================================================ */

/**
 * Register an adapter (language binding)
 * 
 * Each language binding registers itself with a unique adapter_id.
 * Used internally for tracking which binding generated which events.
 */
memwatch_adapter_id memwatch_register_adapter(const char *name);

/**
 * Unregister an adapter
 */
void memwatch_unregister_adapter(memwatch_adapter_id adapter_id);

/* ============================================================================
 * Helper Macros (for language bindings)
 * ============================================================================ */

#define MEMWATCH_OK 0
#define MEMWATCH_ERR_NOT_INIT -1
#define MEMWATCH_ERR_INVALID_ADDR -2
#define MEMWATCH_ERR_NO_MEMORY -3
#define MEMWATCH_ERR_MPROTECT -4
#define MEMWATCH_ERR_NOT_FOUND -5

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MEMWATCH_UNIFIED_H */
