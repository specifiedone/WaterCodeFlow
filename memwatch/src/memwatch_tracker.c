/*
 * memwatch_tracker_v2.c - Simplified memory tracker using periodic sampling
 * 
 * Instead of mprotect + SIGSEGV (complex signal handling):
 * - Spawn monitoring thread
 * - Periodically snapshot tracked memory regions
 * - Detect and log changes
 * - Store in FastStorage (ultra-fast mmap-based KV store)
 * 
 * This avoids the instruction-pointer stepping problem entirely!
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sqlite3.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_TRACKED_REGIONS 256
#define MAX_EVENTS_BEFORE_FLUSH 1000
#define SAMPLING_INTERVAL_US 10000  /* 10ms */

typedef struct {
    uint64_t address;
    size_t size;
    uint8_t *old_data;
    uint8_t *current_data;
    char name[64];
    uint32_t region_id;
    bool is_tracking;
    uint32_t change_count;
} tracked_region_t;

typedef struct {
    uint64_t timestamp_ms;
    uint32_t region_id;
    uint64_t fault_address;
    uint64_t old_value;
    uint64_t new_value;
    uint32_t offset;
    uint8_t thread_id;
    char scope[16];
    /* Enhanced execution context */
    uint64_t step_id;
    char file_name[256];
    char function_name[256];
    int line_number;
} memory_event_t;

typedef struct {
    tracked_region_t regions[MAX_TRACKED_REGIONS];
    int region_count;
    
    memory_event_t event_buffer[MAX_EVENTS_BEFORE_FLUSH];
    int event_count;
    
    sqlite3 *db;
    int use_faststorage;  /* Use FastStorage instead of SQLite */
    pthread_mutex_t lock;
    
    bool track_all_vars;
    bool track_sql;
    bool track_threads;
    char scope_filter[16];
    
    bool monitoring_active;
    pthread_t monitor_thread;
} tracker_state_t;

static tracker_state_t g_tracker = {0};

/* Thread-local execution context */
static __thread uint64_t tl_step_id = 0;
static __thread char tl_current_file[256] = {0};
static __thread char tl_current_function[256] = {0};
static __thread int tl_current_line = 0;

/* ============================================================================
 * Database Functions
 * ============================================================================ */

static int init_database(const char *path) {
    /* Using SQLite for C code */
    printf("✅ Using SQLite backend\n");
    g_tracker.use_faststorage = 0;
    
    int rc = sqlite3_open(path, &g_tracker.db);
    if (rc) {
        fprintf(stderr, "❌ Database open failed: %s\n", sqlite3_errmsg(g_tracker.db));
        return -1;
    }

    const char *sql = 
        "CREATE TABLE IF NOT EXISTS memory_changes (" \
        "    id INTEGER PRIMARY KEY AUTOINCREMENT," \
        "    timestamp_ms INTEGER NOT NULL," \
        "    region_id INTEGER NOT NULL," \
        "    region_name TEXT," \
        "    offset INTEGER," \
        "    old_value TEXT," \
        "    new_value TEXT," \
        "    thread_id INTEGER," \
        "    scope TEXT," \
        "    change_count INTEGER" \
        ");" \
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON memory_changes(timestamp_ms);" \
        "CREATE INDEX IF NOT EXISTS idx_region ON memory_changes(region_id);" \
        "CREATE TABLE IF NOT EXISTS sql_queries (" \
        "    id INTEGER PRIMARY KEY AUTOINCREMENT," \
        "    timestamp_ms INTEGER NOT NULL," \
        "    query_text TEXT NOT NULL," \
        "    query_type TEXT," \
        "    thread_id INTEGER" \
        ");" \
        "CREATE INDEX IF NOT EXISTS idx_sql_timestamp ON sql_queries(timestamp_ms);";

    char *err = NULL;
    rc = sqlite3_exec(g_tracker.db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ SQL error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    return 0;
}

static int get_event_count_from_database(void) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM memory_changes";
    int rc = sqlite3_prepare_v2(g_tracker.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }
    
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

static void flush_events_to_database(void) {
    if (g_tracker.event_count == 0) return;

    for (int i = 0; i < g_tracker.event_count; i++) {
        memory_event_t *evt = &g_tracker.event_buffer[i];
        tracked_region_t *region = &g_tracker.regions[evt->region_id];

        char old_str[32], new_str[32];
        snprintf(old_str, sizeof(old_str), "0x%016lx", evt->old_value);
        snprintf(new_str, sizeof(new_str), "0x%016lx", evt->new_value);

        if (g_tracker.use_faststorage) {
            /* Store in FastStorage (key-value format with execution context) */
            char key[128], value[1024];
            static int event_id = 0;
            snprintf(key, sizeof(key), "mem:%d", event_id++);
            snprintf(value, sizeof(value), "%ld|%d|%s|%d|%s|%s|%d|%s|%d|%ld|%s|%s|%d",
                     evt->timestamp_ms, evt->region_id, region->name, evt->offset,
                     old_str, new_str, evt->thread_id, evt->scope, region->change_count,
                     evt->step_id, 
                     evt->file_name[0] ? evt->file_name : "none",
                     evt->function_name[0] ? evt->function_name : "none",
                     evt->line_number);
        } else {
            /* Store in SQLite (traditional SQL) - create table if needed */
            const char *create_table = 
                "CREATE TABLE IF NOT EXISTS memory_changes_detailed ("
                "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "    timestamp_ms INTEGER,"
                "    region_id INTEGER,"
                "    region_name TEXT,"
                "    offset INTEGER,"
                "    old_value TEXT,"
                "    new_value TEXT,"
                "    thread_id INTEGER,"
                "    scope TEXT,"
                "    change_count INTEGER,"
                "    step_id INTEGER,"
                "    file_name TEXT,"
                "    function_name TEXT,"
                "    line_number INTEGER"
                ");";
            
            sqlite3_exec(g_tracker.db, create_table, NULL, NULL, NULL);
            
            const char *sql = "INSERT INTO memory_changes_detailed "
                "(timestamp_ms, region_id, region_name, offset, old_value, new_value, thread_id, scope, change_count, step_id, file_name, function_name, line_number) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            sqlite3_stmt *stmt;
            int rc = sqlite3_prepare_v2(g_tracker.db, sql, -1, &stmt, NULL);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(g_tracker.db));
                continue;
            }

            sqlite3_bind_int64(stmt, 1, evt->timestamp_ms);
            sqlite3_bind_int(stmt, 2, evt->region_id);
            sqlite3_bind_text(stmt, 3, region->name, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 4, evt->offset);
            sqlite3_bind_text(stmt, 5, old_str, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, new_str, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, evt->thread_id);
            sqlite3_bind_text(stmt, 8, evt->scope, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 9, region->change_count);
            sqlite3_bind_int64(stmt, 10, evt->step_id);
            sqlite3_bind_text(stmt, 11, evt->file_name[0] ? evt->file_name : "none", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 12, evt->function_name[0] ? evt->function_name : "none", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 13, evt->line_number);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "Step error: %s\n", sqlite3_errmsg(g_tracker.db));
            }
            sqlite3_finalize(stmt);
        }
    }

    g_tracker.event_count = 0;
}

/* ============================================================================
 * Memory Monitoring Thread
 * ============================================================================ */

static void* monitor_thread_func(void *arg) {
    (void)arg;

    while (g_tracker.monitoring_active) {
        usleep(SAMPLING_INTERVAL_US);  /* Sample every 10ms */

        pthread_mutex_lock(&g_tracker.lock);

        /* Check each tracked region for changes */
        for (int i = 0; i < g_tracker.region_count; i++) {
            tracked_region_t *region = &g_tracker.regions[i];
            if (!region->is_tracking) continue;

            /* Copy current memory state */
            memcpy(region->current_data, (void *)region->address, region->size);

            /* Compare with previous state */
            for (size_t offset = 0; offset < region->size; offset += 8) {
                size_t cmp_size = (region->size - offset < 8) ? (region->size - offset) : 8;
                
                uint64_t old_val = 0, new_val = 0;
                memcpy(&old_val, &region->old_data[offset], cmp_size);
                memcpy(&new_val, &region->current_data[offset], cmp_size);

                if (old_val != new_val) {
                    /* Change detected */
                    if (g_tracker.event_count < MAX_EVENTS_BEFORE_FLUSH) {
                        memory_event_t *evt = &g_tracker.event_buffer[g_tracker.event_count++];
                        
                        evt->timestamp_ms = (uint64_t)time(NULL) * 1000;
                        evt->region_id = region->region_id;
                        evt->fault_address = region->address + offset;
                        evt->old_value = old_val;
                        evt->new_value = new_val;
                        evt->offset = offset;
                        evt->thread_id = (uint32_t)(pthread_self() & 0xFF);
                        strcpy(evt->scope, g_tracker.scope_filter);
                        
                        /* Capture execution context */
                        evt->step_id = tl_step_id;
                        strncpy(evt->file_name, tl_current_file, sizeof(evt->file_name) - 1);
                        strncpy(evt->function_name, tl_current_function, sizeof(evt->function_name) - 1);
                        evt->line_number = tl_current_line;

                        region->change_count++;

                        printf("  [TRACKED] %s[%zu]: 0x%lx -> 0x%lx | step:%ld | %s:%d in %s()\n", 
                               region->name, offset, old_val, new_val, evt->step_id,
                               evt->file_name[0] ? evt->file_name : "?",
                               evt->line_number,
                               evt->function_name[0] ? evt->function_name : "?");
                    }

                    /* Update old data */
                    memcpy(&region->old_data[offset], &new_val, cmp_size);

                    if (g_tracker.event_count >= MAX_EVENTS_BEFORE_FLUSH) {
                        pthread_mutex_unlock(&g_tracker.lock);
                        flush_events_to_database();
                        pthread_mutex_lock(&g_tracker.lock);
                    }
                }
            }
        }

        pthread_mutex_unlock(&g_tracker.lock);
    }

    return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

int tracker_init(const char *db_path, bool track_all, bool track_sql, bool track_threads, const char *scope) {
    if (init_database(db_path) < 0) {
        return -1;
    }

    g_tracker.track_all_vars = track_all;
    g_tracker.track_sql = track_sql;
    g_tracker.track_threads = track_threads;
    strncpy(g_tracker.scope_filter, scope, sizeof(g_tracker.scope_filter) - 1);

    g_tracker.monitoring_active = true;

    /* Start monitoring thread */
    int rc = pthread_create(&g_tracker.monitor_thread, NULL, monitor_thread_func, NULL);
    if (rc != 0) {
        perror("pthread_create failed");
        return -1;
    }

    return 0;
}

int tracker_watch(uint64_t address, size_t size, const char *name) {
    if (g_tracker.region_count >= MAX_TRACKED_REGIONS) {
        fprintf(stderr, "❌ Too many tracked regions\n");
        return -1;
    }

    tracked_region_t *region = &g_tracker.regions[g_tracker.region_count];

    region->address = address;
    region->size = size;
    region->region_id = g_tracker.region_count;
    region->change_count = 0;
    strncpy(region->name, name, sizeof(region->name) - 1);

    /* Copy initial data */
    region->old_data = malloc(size);
    region->current_data = malloc(size);
    memcpy(region->old_data, (void *)address, size);
    memcpy(region->current_data, (void *)address, size);

    region->is_tracking = true;
    g_tracker.region_count++;

    printf("✅ Tracking: %s @ 0x%lx (%zu bytes)\n", name, address, size);
    return 0;
}

int tracker_unwatch(int region_id) {
    if (region_id < 0 || region_id >= g_tracker.region_count) {
        return -1;
    }

    tracked_region_t *region = &g_tracker.regions[region_id];
    if (!region->is_tracking) return -1;

    free(region->old_data);
    free(region->current_data);

    region->is_tracking = false;
    return 0;
}

void tracker_close(void) {
    g_tracker.monitoring_active = false;
    pthread_join(g_tracker.monitor_thread, NULL);

    /* Flush remaining events */
    flush_events_to_database();

    /* Restore all regions */
    for (int i = 0; i < g_tracker.region_count; i++) {
        if (g_tracker.regions[i].is_tracking) {
            tracker_unwatch(i);
        }
    }

    /* Close storage backend */
    if (g_tracker.db) {
        sqlite3_close(g_tracker.db);
    }

    printf("\n✅ Tracking stopped\n");
    printf("   Total events: %d\n", g_tracker.event_count);
}

int tracker_get_event_count(void) {
    return get_event_count_from_database();
}

void tracker_log_sql_query(const char *query_text) {
    if (!query_text || !g_tracker.track_sql) {
        return;
    }

    /* Check if we have a storage backend */
    if (!g_tracker.use_faststorage && !g_tracker.db) {
        return;
    }

    /* Determine query type */
    const char *query_type = "UNKNOWN";
    if (strncasecmp(query_text, "SELECT", 6) == 0) {
        query_type = "SELECT";
    } else if (strncasecmp(query_text, "INSERT", 6) == 0) {
        query_type = "INSERT";
    } else if (strncasecmp(query_text, "UPDATE", 6) == 0) {
        query_type = "UPDATE";
    } else if (strncasecmp(query_text, "DELETE", 6) == 0) {
        query_type = "DELETE";
    } else if (strncasecmp(query_text, "CREATE", 6) == 0) {
        query_type = "CREATE";
    } else if (strncasecmp(query_text, "DROP", 4) == 0) {
        query_type = "DROP";
    } else if (strncasecmp(query_text, "ALTER", 5) == 0) {
        query_type = "ALTER";
    }

    /* Get current thread ID */
    long thread_id = (long)pthread_self();

    /* Get timestamp in milliseconds */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long timestamp_ms = (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);

    if (g_tracker.db) {
        /* Store in SQLite */
        char query[2048];
        snprintf(query, sizeof(query),
                 "INSERT INTO sql_queries (timestamp_ms, query_text, query_type, thread_id) "
                 "VALUES (%lld, '%s', '%s', %ld);",
                 timestamp_ms, query_text, query_type, thread_id);

        char *err_msg = NULL;
        if (sqlite3_exec(g_tracker.db, query, NULL, NULL, &err_msg) != SQLITE_OK) {
            if (err_msg) {
                sqlite3_free(err_msg);
            }
        }
    }
}

/* ============================================================================
 * Execution Context Tracking (for enhanced variable change tracking)
 * ============================================================================ */

void tracker_step(void) {
    /* Increment step counter for current thread */
    tl_step_id++;
}

void tracker_set_context(const char *filename, const char *funcname, int line_num) {
    if (filename) {
        strncpy(tl_current_file, filename, sizeof(tl_current_file) - 1);
        tl_current_file[sizeof(tl_current_file) - 1] = 0;
    }
    if (funcname) {
        strncpy(tl_current_function, funcname, sizeof(tl_current_function) - 1);
        tl_current_function[sizeof(tl_current_function) - 1] = 0;
    }
    tl_current_line = line_num;
}

void tracker_func_enter(const char *func_name, const char *file_name) {
    tracker_set_context(file_name, func_name, 0);
    tracker_step();
    if (getenv("MEMWATCH_DEBUG_FUNCS")) {
        printf("[TRACKER] ENTER: %s @ %s\n", func_name, file_name);
    }
}

void tracker_func_exit(const char *func_name) {
    tracker_step();
    if (getenv("MEMWATCH_DEBUG_FUNCS")) {
        printf("[TRACKER] EXIT: %s\n", func_name);
    }
}
