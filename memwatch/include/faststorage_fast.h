/*
 * faststorage_fast.h - Ultra-fast mmap-based KV storage in pure C
 * 
 * This is a high-performance, production-grade storage engine that:
 * - Uses memory-mapped files for O(1) random access
 * - Supports concurrent reads and writes
 * - Recovers from crashes automatically
 * - Works from C, Python, and any language via dlopen
 * 
 * Author: Optimized for pure speed
 * Date: 2026
 */

#ifndef FASTSTORAGE_FAST_H
#define FASTSTORAGE_FAST_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to storage instance */
typedef struct FastStorageImpl {
    int fd;
    uint8_t *mmap_ptr;
    size_t file_size;
    void *header;
    void *hash_table;
    pthread_rwlock_t lock;
    uint64_t reads;
    uint64_t writes;
    uint64_t deletes;
    int resized;
} FastStorageImpl, FastStorage;

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================ */

/**
 * Create or open a FastStorage instance
 * 
 * @param filename Path to the storage file
 * @param capacity Initial capacity in bytes (will auto-grow)
 * @return Handle to storage, NULL on failure
 */
FastStorage* faststorage_create(const char *filename, size_t capacity);

/**
 * Close and flush storage
 * 
 * @param fs Storage handle
 */
void faststorage_destroy(FastStorage *fs);

/* ============================================================================
 * WRITE/READ OPERATIONS - Ultra-fast paths
 * ============================================================================ */

/**
 * Write key-value pair (overwrites if exists)
 * Returns immediately after writing to mmap buffer
 * 
 * @param fs Storage handle
 * @param key Null-terminated key (max 256 bytes)
 * @param value Arbitrary binary value
 * @param value_len Length of value in bytes
 * @return 0 on success, -1 on failure
 */
int faststorage_write(FastStorage *fs, const char *key, const void *value, size_t value_len);

/**
 * Read value by key
 * O(1) lookup via internal hash map
 * 
 * @param fs Storage handle
 * @param key Null-terminated key
 * @param value_out Output buffer (caller allocated)
 * @param value_len_out Receives actual length (set to buffer size before call)
 * @return 0 on success, -1 if not found or buffer too small
 */
int faststorage_read(FastStorage *fs, const char *key, void *value_out, size_t *value_len_out);

/**
 * Read value size without copying
 * 
 * @param fs Storage handle
 * @param key Null-terminated key
 * @return Size in bytes, or -1 if not found
 */
ssize_t faststorage_size(FastStorage *fs, const char *key);

/**
 * Delete a key-value pair
 * 
 * @param fs Storage handle
 * @param key Null-terminated key
 * @return 0 on success, -1 if not found
 */
int faststorage_delete(FastStorage *fs, const char *key);

/**
 * Check if key exists
 * 
 * @param fs Storage handle
 * @param key Null-terminated key
 * @return 1 if exists, 0 if not
 */
int faststorage_exists(FastStorage *fs, const char *key);

/* ============================================================================
 * MAINTENANCE FUNCTIONS
 * ============================================================================ */

/**
 * Flush all pending writes to disk
 * This is automatic but can be called for safety
 * 
 * @param fs Storage handle
 * @return 0 on success
 */
int faststorage_flush(FastStorage *fs);

/**
 * Get current number of key-value pairs
 * 
 * @param fs Storage handle
 * @return Count of pairs
 */
size_t faststorage_count(FastStorage *fs);

/**
 * Get bytes currently used
 * 
 * @param fs Storage handle
 * @return Bytes used (not including allocated-but-unused)
 */
size_t faststorage_bytes_used(FastStorage *fs);

/**
 * Get total capacity
 * 
 * @param fs Storage handle
 * @return Total capacity in bytes
 */
size_t faststorage_capacity(FastStorage *fs);

/**
 * Clear all data (keep file)
 * 
 * @param fs Storage handle
 * @return 0 on success
 */
int faststorage_clear(FastStorage *fs);

/**
 * Compact storage (rebuilds to remove fragmentation)
 * This may take time but speeds up future reads
 * 
 * @param fs Storage handle
 * @return 0 on success
 */
int faststorage_compact(FastStorage *fs);

/* ============================================================================
 * STATISTICS & DEBUGGING
 * ============================================================================ */

/**
 * Get statistics (optional, for benchmarking)
 * 
 * @param fs Storage handle
 * @param stats Output structure (caller allocated)
 * @return 0 on success
 */
typedef struct {
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_deletes;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t compactions;
    uint64_t growth_count;
} FastStorageStats;

int faststorage_get_stats(FastStorage *fs, FastStorageStats *stats);

/**
 * Reset statistics
 * 
 * @param fs Storage handle
 */
void faststorage_reset_stats(FastStorage *fs);

#ifdef __cplusplus
}
#endif

#endif /* FASTSTORAGE_FAST_H */
