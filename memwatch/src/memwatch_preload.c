/*
 * memwatch_preload.c - LD_PRELOAD library for automatic program instrumentation
 * 
 * Usage: LD_PRELOAD=./libmemwatch.so MEMWATCH_DB=data.db MEMWATCH_VARS=1 ./program
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include "memwatch_tracker.h"

static int initialized = 0;

__attribute__((constructor))
static void memwatch_init(void) {
    const char *db_path = getenv("MEMWATCH_DB");
    if (!db_path) {
        fprintf(stderr, "[memwatch] MEMWATCH_DB not set, skipping\n");
        return;
    }

    const char *track_vars_str = getenv("MEMWATCH_VARS");
    const char *track_sql_str = getenv("MEMWATCH_SQL");
    const char *track_threads_str = getenv("MEMWATCH_THREADS");
    const char *scope_str = getenv("MEMWATCH_SCOPE");

    bool track_vars = track_vars_str && atoi(track_vars_str);
    bool track_sql = track_sql_str && atoi(track_sql_str);
    bool track_threads = track_threads_str && atoi(track_threads_str);
    const char *scope = scope_str ?: "both";

    fprintf(stderr, "[memwatch] Initializing: db=%s, vars=%d, sql=%d, threads=%d, scope=%s\n",
            db_path, track_vars, track_sql, track_threads, scope);

    if (tracker_init(db_path, track_vars, track_sql, track_threads, scope) < 0) {
        fprintf(stderr, "[memwatch] Failed to initialize tracker\n");
        return;
    }

    initialized = 1;
    fprintf(stderr, "[memwatch] Ready for tracking\n");
}

__attribute__((destructor))
static void memwatch_fini(void) {
    if (initialized) {
        fprintf(stderr, "[memwatch] Finalizing...\n");
        tracker_close();
    }
}

/* Hook malloc to auto-track allocations if --track-all-vars is enabled */
void* malloc(size_t size) {
    extern void *__libc_malloc(size_t);
    void *ptr = __libc_malloc(size);
    
    if (initialized && getenv("MEMWATCH_AUTO_TRACK")) {
        char name[64];
        snprintf(name, sizeof(name), "malloc_%p", ptr);
        
        /* Allocate in page-aligned chunks for mprotect to work */
        size_t pagesize = getpagesize();
        if (size >= pagesize) {
            tracker_watch((uint64_t)ptr, (size / pagesize) * pagesize, name);
        }
    }
    
    return ptr;
}

/* Hook sqlite3_exec to capture SQL queries */
typedef int (*sqlite3_exec_func)(void *, const char *, void *, void *, char **);
static sqlite3_exec_func real_sqlite3_exec = NULL;
static __thread int in_sqlite3_exec = 0;  /* Thread-local flag to prevent recursion */

int sqlite3_exec(void *db, const char *sql, void *callback, void *arg, char **err) {
    if (!real_sqlite3_exec) {
        real_sqlite3_exec = (sqlite3_exec_func)dlsym(RTLD_NEXT, "sqlite3_exec");
    }
    
    int result;
    
    /* Prevent infinite recursion when tracker logs SQL */
    if (!in_sqlite3_exec && initialized && sql) {
        in_sqlite3_exec = 1;
        tracker_log_sql_query(sql);
        in_sqlite3_exec = 0;
    }
    
    result = real_sqlite3_exec(db, sql, callback, arg, err);
    return result;
}

/* Hook sqlite3_prepare_v2 to capture prepared statements */
typedef int (*sqlite3_prepare_v2_func)(void *, const char *, int, void **, const char **);
static sqlite3_prepare_v2_func real_sqlite3_prepare_v2 = NULL;
static __thread int in_sqlite3_prepare = 0;  /* Thread-local flag to prevent recursion */

int sqlite3_prepare_v2(void *db, const char *sql, int nbyte, void **ppstmt, const char **pztail) {
    if (!real_sqlite3_prepare_v2) {
        real_sqlite3_prepare_v2 = (sqlite3_prepare_v2_func)dlsym(RTLD_NEXT, "sqlite3_prepare_v2");
    }
    
    int result;
    
    /* Prevent infinite recursion when tracker logs SQL */
    if (!in_sqlite3_prepare && initialized && sql) {
        in_sqlite3_prepare = 1;
        tracker_log_sql_query(sql);
        in_sqlite3_prepare = 0;
    }
    
    result = real_sqlite3_prepare_v2(db, sql, nbyte, ppstmt, pztail);
    return result;
}

