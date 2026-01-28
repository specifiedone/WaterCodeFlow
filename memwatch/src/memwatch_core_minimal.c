/*
 * memwatch_core_minimal.c - Minimal memwatch implementation for CLI
 * 
 * This is a simplified version that implements the unified API
 * without Python dependencies. Full-featured version is in memwatch.c
 */

#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "memwatch_unified.h"

#define RING_CAPACITY 65536
#define PAGE_SIZE 4096
#define PREVIEW_SIZE 256
#define MAX_REGIONS 4096

/* Ring entry */
typedef struct {
    uintptr_t page_start;
    uint32_t region_id;
    uint64_t timestamp_ns;
} PageEvent;

/* Tracked region */
typedef struct {
    uint64_t addr;
    size_t size;
    const char *name;
    uint32_t region_id;
    void *user_data;
    bool active;
    uint8_t *last_snapshot;
} TrackedRegion;

/* Global state */
static struct {
    PageEvent *ring;
    atomic_uint ring_head;
    atomic_uint ring_tail;
    
    TrackedRegion regions[MAX_REGIONS];
    uint32_t next_region_id;
    pthread_mutex_t regions_mutex;
    
    pthread_t worker_thread;
    atomic_bool worker_running;
    atomic_bool shutdown_requested;
    
    memwatch_callback_t callback;
    void *callback_ctx;
    pthread_mutex_t callback_mutex;
    
} g_state = {0};

/* Signal handler */
static void sigsegv_handler(int sig, siginfo_t *info, void *uctx) {
    (void)sig;
    (void)info;
    (void)uctx;
    
    /* Just record in ring and continue */
    unsigned head = atomic_load(&g_state.ring_head);
    if (head + 1 < RING_CAPACITY) {
        g_state.ring[head].timestamp_ns = (uint64_t)time(NULL) * 1000000000ULL;
        atomic_store(&g_state.ring_head, head + 1);
    }
}

/* Worker thread */
static void* worker_thread_fn(void *arg) {
    (void)arg;
    
    while (atomic_load(&g_state.worker_running)) {
        unsigned tail = atomic_load(&g_state.ring_tail);
        unsigned head = atomic_load(&g_state.ring_head);
        
        if (tail < head) {
            PageEvent *evt = &g_state.ring[tail];
            
            /* Find region and trigger callback */
            for (int i = 0; i < MAX_REGIONS; i++) {
                if (g_state.regions[i].active && 
                    g_state.regions[i].region_id == evt->region_id) {
                    
                    TrackedRegion *region = &g_state.regions[i];
                    
                    /* Create event */
                    memwatch_change_event_t event = {
                        .seq = tail,
                        .timestamp_ns = evt->timestamp_ns,
                        .region_id = region->region_id,
                        .variable_name = region->name,
                        .old_preview = (uint8_t *)"changed",
                        .old_preview_size = 7,
                        .new_preview = (uint8_t *)"value",
                        .new_preview_size = 5,
                        .user_data = region->user_data,
                    };
                    
                    pthread_mutex_lock(&g_state.callback_mutex);
                    if (g_state.callback) {
                        g_state.callback(&event, g_state.callback_ctx);
                    }
                    pthread_mutex_unlock(&g_state.callback_mutex);
                    
                    break;
                }
            }
            
            atomic_store(&g_state.ring_tail, tail + 1);
        }
        
        usleep(10000);  /* 10ms */
    }
    
    return NULL;
}

/* API Implementation */

int memwatch_init(void) {
    if (g_state.ring) {
        return 0;  /* Already initialized */
    }
    
    g_state.ring = calloc(RING_CAPACITY, sizeof(PageEvent));
    if (!g_state.ring) {
        return -1;
    }
    
    pthread_mutex_init(&g_state.regions_mutex, NULL);
    pthread_mutex_init(&g_state.callback_mutex, NULL);
    
    atomic_store(&g_state.worker_running, true);
    pthread_create(&g_state.worker_thread, NULL, worker_thread_fn, NULL);
    
    /* Install signal handler */
    struct sigaction sa = {0};
    sa.sa_sigaction = sigsegv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    
    return 0;
}

void memwatch_shutdown(void) {
    if (!g_state.ring) {
        return;
    }
    
    atomic_store(&g_state.shutdown_requested, true);
    atomic_store(&g_state.worker_running, false);
    
    pthread_join(g_state.worker_thread, NULL);
    
    free(g_state.ring);
    g_state.ring = NULL;
    
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (g_state.regions[i].last_snapshot) {
            free(g_state.regions[i].last_snapshot);
        }
    }
    
    pthread_mutex_destroy(&g_state.regions_mutex);
    pthread_mutex_destroy(&g_state.callback_mutex);
}

memwatch_region_id memwatch_watch(uint64_t addr, size_t size, 
                                  const char *name, void *user_data) {
    if (!g_state.ring) {
        return 0;
    }
    
    pthread_mutex_lock(&g_state.regions_mutex);
    
    uint32_t region_id = 0;
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (!g_state.regions[i].active) {
            region_id = i + 1;
            g_state.regions[i].addr = addr;
            g_state.regions[i].size = size;
            g_state.regions[i].name = name;
            g_state.regions[i].region_id = region_id;
            g_state.regions[i].user_data = user_data;
            g_state.regions[i].last_snapshot = malloc(size < 256 ? size : 256);
            g_state.regions[i].active = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_state.regions_mutex);
    
    return region_id;
}

bool memwatch_unwatch(memwatch_region_id region_id) {
    pthread_mutex_lock(&g_state.regions_mutex);
    
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (g_state.regions[i].active && g_state.regions[i].region_id == region_id) {
            g_state.regions[i].active = false;
            if (g_state.regions[i].last_snapshot) {
                free(g_state.regions[i].last_snapshot);
                g_state.regions[i].last_snapshot = NULL;
            }
            pthread_mutex_unlock(&g_state.regions_mutex);
            return true;
        }
    }
    
    pthread_mutex_unlock(&g_state.regions_mutex);
    return false;
}

int memwatch_set_callback(memwatch_callback_t callback, void *user_ctx) {
    pthread_mutex_lock(&g_state.callback_mutex);
    g_state.callback = callback;
    g_state.callback_ctx = user_ctx;
    pthread_mutex_unlock(&g_state.callback_mutex);
    return 0;
}

int memwatch_check_changes(memwatch_change_event_t *out_events, int max_events) {
    (void)out_events;
    (void)max_events;
    return 0;  /* Polling not implemented in minimal version */
}

int memwatch_get_stats(memwatch_stats_t *out_stats) {
    if (!out_stats) return -1;
    
    memset(out_stats, 0, sizeof(*out_stats));
    
    pthread_mutex_lock(&g_state.regions_mutex);
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (g_state.regions[i].active) {
            out_stats->num_tracked_regions++;
        }
    }
    pthread_mutex_unlock(&g_state.regions_mutex);
    
    out_stats->total_events = atomic_load(&g_state.ring_head);
    
    return 0;
}