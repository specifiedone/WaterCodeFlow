/*
 * faststorage_bridge.c - C wrapper for FastStorage
 * Bridges memwatch tracker to ultra-fast mmap-based storage
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faststorage_fast.h"

static FastStorage *_default_storage = NULL;
static int _faststorage_initialized = 0;

/* Initialize FastStorage interface */
int faststorage_bridge_init(const char *db_path, size_t capacity) {
    if (_faststorage_initialized) {
        return 0;
    }

    /* Use the new pure-C FastStorage implementation */
    _default_storage = faststorage_create(db_path, capacity);
    
    if (_default_storage) {
        _faststorage_initialized = 1;
        fprintf(stderr, "✅ Using FastStorage backend (pure C mmap, ultra-fast)\n");
        return 0;
    } else {
        fprintf(stderr, "⚠️  FastStorage creation failed, would fallback to SQLite\n");
        return -1;
    }
}

/* Write key-value pair to storage */
int faststorage_bridge_write(const char *key, const char *value) {
    if (!_faststorage_initialized || !_default_storage) {
        return -1;
    }

    if (!value) {
        value = "";
    }
    
    return faststorage_write(_default_storage, key, value, strlen(value));
}

/* Read value from storage */
const char* faststorage_bridge_read(const char *key) {
    if (!_faststorage_initialized || !_default_storage) {
        return NULL;
    }

    /* Note: This is a simplified version that doesn't handle binary data well
       For production, use faststorage_read() directly with a buffer */
    static char buffer[102400];
    size_t len = sizeof(buffer);
    
    if (faststorage_read(_default_storage, key, buffer, &len) == 0) {
        buffer[len] = 0;
        return buffer;
    }
    
    return NULL;
}

/* Flush storage to disk */
int faststorage_bridge_flush(void) {
    if (!_faststorage_initialized || !_default_storage) {
        return 0;
    }

    return faststorage_flush(_default_storage);
}

/* Get bytes used */
size_t faststorage_bridge_bytes_used(void) {
    if (!_faststorage_initialized || !_default_storage) {
        return 0;
    }

    return faststorage_bytes_used(_default_storage);
}

/* Get utilization percentage */
float faststorage_bridge_utilization(size_t capacity) {
    size_t used = faststorage_bridge_bytes_used();
    if (capacity == 0) return 0.0;
    return ((float)used / capacity) * 100.0;
}

/* Cleanup */
void faststorage_bridge_close(void) {
    if (_faststorage_initialized && _default_storage) {
        faststorage_flush(_default_storage);
        faststorage_destroy(_default_storage);
        _default_storage = NULL;
        _faststorage_initialized = 0;
        fprintf(stderr, "✅ FastStorage closed and flushed\n");
    }
}