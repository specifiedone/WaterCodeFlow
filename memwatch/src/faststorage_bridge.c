/*
 * faststorage_bridge.c - C wrapper for FastStorage C++ implementation
 * Bridges the memwatch tracker to the high-performance mmap-based storage
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

/* Forward declarations for FastStorage C interface */
typedef void* FastStorageHandle;

/* Function pointers to FastStorage C functions */
static FastStorageHandle (*faststorage_create)(const char* path, size_t capacity) = NULL;
static void (*faststorage_destroy)(FastStorageHandle handle) = NULL;
static int (*faststorage_write)(FastStorageHandle handle, const char* key, const char* value) = NULL;
static const char* (*faststorage_read)(FastStorageHandle handle, const char* key) = NULL;
static int (*faststorage_flush)(FastStorageHandle handle) = NULL;
static size_t (*faststorage_bytes_used)(FastStorageHandle handle) = NULL;

static int _faststorage_initialized = 0;
static FastStorageHandle _default_storage = NULL;

/* Initialize FastStorage interface */
int faststorage_bridge_init(const char *db_path, size_t capacity) {
    if (_faststorage_initialized) {
        return 0;
    }

    /* Try multiple paths for FastStorage shared library */
    const char *lib_paths[] = {
        "./build/lib_faststorage.so",
        "../build/lib_faststorage.so",
        "/usr/local/lib/lib_faststorage.so",
        "lib_faststorage.so",
        NULL
    };
    
    void *handle = NULL;
    for (int i = 0; lib_paths[i] != NULL; i++) {
        handle = dlopen(lib_paths[i], RTLD_LAZY);
        if (handle) {
            fprintf(stderr, "[FastStorage] Loaded library from: %s\n", lib_paths[i]);
            break;
        }
    }
    
    if (!handle) {
        fprintf(stderr, "[FastStorage] Warning: Could not load FastStorage library\n");
        fprintf(stderr, "[FastStorage] Falling back to SQLite\n");
        return -1;
    }

    /* Load function pointers */
    faststorage_create = dlsym(handle, "faststorage_create");
    faststorage_destroy = dlsym(handle, "faststorage_destroy");
    faststorage_write = dlsym(handle, "faststorage_write");
    faststorage_read = dlsym(handle, "faststorage_read");
    faststorage_flush = dlsym(handle, "faststorage_flush");
    faststorage_bytes_used = dlsym(handle, "faststorage_bytes_used");

    if (!faststorage_create || !faststorage_destroy || !faststorage_write || !faststorage_read) {
        fprintf(stderr, "[FastStorage] Error: Failed to load FastStorage functions\n");
        dlclose(handle);
        return -1;
    }

    /* Create storage instance */
    _default_storage = faststorage_create(db_path, capacity);
    if (!_default_storage) {
        fprintf(stderr, "[FastStorage] Error: Failed to create FastStorage instance\n");
        dlclose(handle);
        return -1;
    }

    _faststorage_initialized = 1;
    fprintf(stderr, "[FastStorage] Initialized: %s (capacity: %zu MB)\n", 
            db_path, capacity / (1024 * 1024));

    return 0;
}

/* Write key-value pair to storage */
int faststorage_bridge_write(const char *key, const char *value) {
    if (!_faststorage_initialized || !_default_storage) {
        return -1;
    }

    return faststorage_write(_default_storage, key, value);
}

/* Read value from storage */
const char* faststorage_bridge_read(const char *key) {
    if (!_faststorage_initialized || !_default_storage) {
        return NULL;
    }

    return faststorage_read(_default_storage, key);
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

/* Cleanup */
void faststorage_bridge_close(void) {
    if (_faststorage_initialized && _default_storage) {
        faststorage_flush(_default_storage);
        faststorage_destroy(_default_storage);
        _default_storage = NULL;
        _faststorage_initialized = 0;
        fprintf(stderr, "[FastStorage] Closed and flushed\n");
    }
}

/* Get utilization percentage */
float faststorage_bridge_utilization(size_t capacity) {
    size_t used = faststorage_bridge_bytes_used();
    if (capacity == 0) return 0.0;
    return ((float)used / capacity) * 100.0;
}
