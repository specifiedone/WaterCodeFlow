/*
 * memwatch.c - Native core for language-agnostic memory change watcher
 * 
 * Architecture:
 * - Signal handler (async-signal-safe): O(1) ring write only
 * - Worker thread: drains ring, computes diffs, delivers events
 * - Page table: maps page_start -> list of tracked regions
 * - Tiny per-region footprint: ~96 bytes
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
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

/* Configuration constants */
#define RING_CAPACITY 65536
#define PAGE_SIZE 4096
#define PREVIEW_SIZE 256
#define SMALL_COPY_THRESHOLD 4096
#define WRITABLE_WINDOW_MS 5
#define MAX_REGIONS_PER_PAGE 16

/* Ring entry - written by signal handler (async-safe) */
typedef struct {
    uintptr_t page_start;
    uintptr_t fault_ip;
    uint32_t adapter_id;
    uint64_t timestamp_ns;
    uint32_t seq;
    uint32_t thread_id;
} PageEvent;

/* Tracked region metadata (~96 bytes) */
typedef struct TrackedRegion {
    uint64_t addr;
    size_t size;
    uint64_t last_hash;
    uint32_t region_id;
    uint32_t adapter_id;
    uint32_t metadata_ref;
    uint32_t epoch;
    struct TrackedRegion *next_in_page;  /* linked list per page */
    uint64_t last_check_time_ns;
} TrackedRegion;

/* Page table entry */
typedef struct {
    uintptr_t page_start;
    TrackedRegion *regions;  /* singly-linked list */
    int region_count;
} PageEntry;

/* Resolver function pointer type */
typedef int (*ResolverFn)(uintptr_t fault_ip, uint32_t adapter_id, 
                          char **file, char **function, int *line, void ***stack);

/* Global state */
static struct {
    /* Ring buffer */
    PageEvent *ring;
    atomic_uint ring_head;
    atomic_uint ring_tail;
    atomic_uint dropped_events;
    atomic_uint seq_counter;
    
    /* Page table (simple hash map) */
    PageEntry *page_table;
    size_t page_table_capacity;
    pthread_mutex_t page_table_mutex;
    
    /* Region tracking */
    TrackedRegion **regions;  /* array indexed by region_id */
    size_t regions_capacity;
    uint32_t next_region_id;
    pthread_mutex_t regions_mutex;
    
    /* Worker thread */
    pthread_t worker_thread;
    atomic_bool worker_running;
    atomic_bool shutdown_requested;
    
    /* Callback */
    PyObject *callback;
    pthread_mutex_t callback_mutex;
    
    /* Resolvers */
    ResolverFn resolvers[256];  /* indexed by adapter_id */
    char *ipc_resolver_paths[256];
    
    /* Statistics */
    atomic_size_t native_memory_bytes;
    atomic_uint tracked_region_count;
    
    /* Protection support */
    bool protection_available;
    struct sigaction old_segv_action;
    
} g_state;

/* Forward declarations */
static void signal_handler(int sig, siginfo_t *si, void *unused);
static void *worker_thread_func(void *arg);
static uint64_t hash_bytes(const void *data, size_t len);
static uint64_t get_monotonic_ns(void);
static PageEntry *page_table_find(uintptr_t page_start);
static void page_table_add_region(uintptr_t page_start, TrackedRegion *region);
static void page_table_remove_region(uintptr_t page_start, TrackedRegion *region);

/* Initialize memwatch core */
static PyObject *mw_init(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    (void)args;  /* Unused in this function */
    
    if (g_state.ring != NULL) {
        Py_RETURN_NONE;  /* already initialized */
    }
    
    /* Allocate ring buffer */
    g_state.ring = calloc(RING_CAPACITY, sizeof(PageEvent));
    if (!g_state.ring) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate ring buffer");
        return NULL;
    }
    atomic_store(&g_state.ring_head, 0);
    atomic_store(&g_state.ring_tail, 0);
    atomic_store(&g_state.dropped_events, 0);
    atomic_store(&g_state.seq_counter, 1);
    
    /* Allocate page table */
    g_state.page_table_capacity = 8192;
    g_state.page_table = calloc(g_state.page_table_capacity, sizeof(PageEntry));
    if (!g_state.page_table) {
        free(g_state.ring);
        g_state.ring = NULL;
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate page table");
        return NULL;
    }
    pthread_mutex_init(&g_state.page_table_mutex, NULL);
    
    /* Allocate regions array */
    g_state.regions_capacity = 1024;
    g_state.regions = calloc(g_state.regions_capacity, sizeof(TrackedRegion*));
    if (!g_state.regions) {
        free(g_state.ring);
        free(g_state.page_table);
        g_state.ring = NULL;
        g_state.page_table = NULL;
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate regions array");
        return NULL;
    }
    g_state.next_region_id = 1;
    pthread_mutex_init(&g_state.regions_mutex, NULL);
    pthread_mutex_init(&g_state.callback_mutex, NULL);
    
    /* Update memory stats */
    size_t mem = RING_CAPACITY * sizeof(PageEvent) + 
                 g_state.page_table_capacity * sizeof(PageEntry) +
                 g_state.regions_capacity * sizeof(TrackedRegion*);
    atomic_store(&g_state.native_memory_bytes, mem);
    
    /* Install signal handler for page protection */
#ifdef __linux__
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler;
    if (sigaction(SIGSEGV, &sa, &g_state.old_segv_action) == 0) {
        g_state.protection_available = true;
    }
#endif
    
    /* Start worker thread */
    atomic_store(&g_state.worker_running, true);
    atomic_store(&g_state.shutdown_requested, false);
    if (pthread_create(&g_state.worker_thread, NULL, worker_thread_func, NULL) != 0) {
        /* Cleanup on failure */
        if (g_state.protection_available) {
            sigaction(SIGSEGV, &g_state.old_segv_action, NULL);
        }
        free(g_state.ring);
        free(g_state.page_table);
        free(g_state.regions);
        pthread_mutex_destroy(&g_state.page_table_mutex);
        pthread_mutex_destroy(&g_state.regions_mutex);
        pthread_mutex_destroy(&g_state.callback_mutex);
        memset(&g_state, 0, sizeof(g_state));
        PyErr_SetString(PyExc_RuntimeError, "Failed to start worker thread");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

/* Shutdown memwatch core */
static PyObject *mw_shutdown(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    (void)args;  /* Unused in this function */
    
    if (!g_state.ring) {
        Py_RETURN_NONE;
    }
    
    /* Signal worker to stop */
    atomic_store(&g_state.shutdown_requested, true);
    pthread_join(g_state.worker_thread, NULL);
    
    /* Restore signal handler */
    if (g_state.protection_available) {
        sigaction(SIGSEGV, &g_state.old_segv_action, NULL);
    }
    
    /* Free resources */
    free(g_state.ring);
    free(g_state.page_table);
    
    pthread_mutex_lock(&g_state.regions_mutex);
    for (size_t i = 0; i < g_state.regions_capacity; i++) {
        if (g_state.regions[i]) {
            free(g_state.regions[i]);
        }
    }
    free(g_state.regions);
    pthread_mutex_unlock(&g_state.regions_mutex);
    
    pthread_mutex_destroy(&g_state.page_table_mutex);
    pthread_mutex_destroy(&g_state.regions_mutex);
    pthread_mutex_destroy(&g_state.callback_mutex);
    
    memset(&g_state, 0, sizeof(g_state));
    
    Py_RETURN_NONE;
}

/* Track a memory region */
static PyObject *mw_track(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    
    uint64_t addr;
    size_t size;
    uint32_t adapter_id, metadata_ref;
    
    if (!PyArg_ParseTuple(args, "KnII", &addr, &size, &adapter_id, &metadata_ref)) {
        return NULL;
    }
    
    if (!g_state.ring) {
        PyErr_SetString(PyExc_RuntimeError, "memwatch not initialized");
        return NULL;
    }
    
    /* Allocate region */
    TrackedRegion *region = calloc(1, sizeof(TrackedRegion));
    if (!region) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate region");
        return NULL;
    }
    
    pthread_mutex_lock(&g_state.regions_mutex);
    
    /* Get region ID */
    uint32_t region_id = g_state.next_region_id++;
    if (region_id >= g_state.regions_capacity) {
        size_t new_cap = g_state.regions_capacity * 2;
        TrackedRegion **new_regions = realloc(g_state.regions, 
                                               new_cap * sizeof(TrackedRegion*));
        if (!new_regions) {
            free(region);
            pthread_mutex_unlock(&g_state.regions_mutex);
            PyErr_SetString(PyExc_MemoryError, "Failed to expand regions array");
            return NULL;
        }
        memset(new_regions + g_state.regions_capacity, 0, 
               (new_cap - g_state.regions_capacity) * sizeof(TrackedRegion*));
        g_state.regions = new_regions;
        g_state.regions_capacity = new_cap;
    }
    
    /* Initialize region */
    region->addr = addr;
    region->size = size;
    region->region_id = region_id;
    region->adapter_id = adapter_id;
    region->metadata_ref = metadata_ref;
    region->epoch = 0;
    region->last_check_time_ns = get_monotonic_ns();
    
    /* Compute initial hash */
    Py_BEGIN_ALLOW_THREADS
    region->last_hash = hash_bytes((void*)addr, size);
    Py_END_ALLOW_THREADS
    
    g_state.regions[region_id] = region;
    atomic_fetch_add(&g_state.tracked_region_count, 1);
    atomic_fetch_add(&g_state.native_memory_bytes, sizeof(TrackedRegion));
    
    pthread_mutex_unlock(&g_state.regions_mutex);
    
    /* Add to page table and set protection */
    uintptr_t page_start = (addr / PAGE_SIZE) * PAGE_SIZE;
    uintptr_t page_end = ((addr + size - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    for (uintptr_t page = page_start; page <= page_end; page += PAGE_SIZE) {
        page_table_add_region(page, region);
        
        /* Set page protection if available */
        if (g_state.protection_available) {
            Py_BEGIN_ALLOW_THREADS
            if (mprotect((void*)page, PAGE_SIZE, PROT_READ) != 0) {
                /* Protection failed - log but continue */
                /* In production, might want to track this */
            }
            Py_END_ALLOW_THREADS
        }
    }
    
    return PyLong_FromUnsignedLong(region_id);
}

/* Untrack a memory region */
static PyObject *mw_untrack(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    
    uint32_t region_id;
    
    if (!PyArg_ParseTuple(args, "I", &region_id)) {
        return NULL;
    }
    
    pthread_mutex_lock(&g_state.regions_mutex);
    
    if (region_id >= g_state.regions_capacity || !g_state.regions[region_id]) {
        pthread_mutex_unlock(&g_state.regions_mutex);
        PyErr_SetString(PyExc_ValueError, "Invalid region_id");
        return NULL;
    }
    
    TrackedRegion *region = g_state.regions[region_id];
    g_state.regions[region_id] = NULL;
    
    pthread_mutex_unlock(&g_state.regions_mutex);
    
    /* Remove from page table and restore protection */
    uintptr_t page_start = (region->addr / PAGE_SIZE) * PAGE_SIZE;
    uintptr_t page_end = ((region->addr + region->size - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    for (uintptr_t page = page_start; page <= page_end; page += PAGE_SIZE) {
        page_table_remove_region(page, region);
    }
    
    free(region);
    atomic_fetch_sub(&g_state.tracked_region_count, 1);
    atomic_fetch_sub(&g_state.native_memory_bytes, sizeof(TrackedRegion));
    
    Py_RETURN_NONE;
}

/* Set Python callback for events */
static PyObject *mw_set_callback(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    
    PyObject *callback;
    
    if (!PyArg_ParseTuple(args, "O", &callback)) {
        return NULL;
    }
    
    pthread_mutex_lock(&g_state.callback_mutex);
    
    if (callback == Py_None) {
        Py_XDECREF(g_state.callback);
        g_state.callback = NULL;
    } else {
        if (!PyCallable_Check(callback)) {
            pthread_mutex_unlock(&g_state.callback_mutex);
            PyErr_SetString(PyExc_TypeError, "Callback must be callable");
            return NULL;
        }
        Py_XDECREF(g_state.callback);
        Py_INCREF(callback);
        g_state.callback = callback;
    }
    
    pthread_mutex_unlock(&g_state.callback_mutex);
    
    Py_RETURN_NONE;
}

/* Get statistics */
static PyObject *mw_get_stats(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    (void)args;  /* Unused in this function */
    
    PyObject *stats = PyDict_New();
    
    PyObject *tracked_obj = PyLong_FromUnsignedLong(atomic_load(&g_state.tracked_region_count));
    PyDict_SetItemString(stats, "tracked_regions", tracked_obj);
    Py_DECREF(tracked_obj);
    
    PyObject *capacity_obj = PyLong_FromUnsignedLong(RING_CAPACITY);
    PyDict_SetItemString(stats, "ring_capacity", capacity_obj);
    Py_DECREF(capacity_obj);
    
    uint32_t head = atomic_load(&g_state.ring_head);
    uint32_t tail = atomic_load(&g_state.ring_tail);
    uint32_t used = (head >= tail) ? (head - tail) : (RING_CAPACITY - tail + head);
    PyObject *used_obj = PyLong_FromUnsignedLong(used);
    PyDict_SetItemString(stats, "ring_used", used_obj);
    Py_DECREF(used_obj);
    
    PyObject *dropped_obj = PyLong_FromUnsignedLong(atomic_load(&g_state.dropped_events));
    PyDict_SetItemString(stats, "dropped_events", dropped_obj);
    Py_DECREF(dropped_obj);
    
    PyObject *mem_obj = PyLong_FromSize_t(atomic_load(&g_state.native_memory_bytes));
    PyDict_SetItemString(stats, "native_memory_bytes", mem_obj);
    Py_DECREF(mem_obj);
    
    PyObject *prot_obj = PyBool_FromLong(g_state.protection_available);
    PyDict_SetItemString(stats, "protection_available", prot_obj);
    Py_DECREF(prot_obj);
    
    return stats;
}

/* Register in-process resolver */
static PyObject *mw_register_resolver(PyObject *self, PyObject *args) {
    (void)self;  /* Unused in this function */
    
    uint32_t adapter_id;
    unsigned long long fnptr;
    
    if (!PyArg_ParseTuple(args, "IK", &adapter_id, &fnptr)) {
        return NULL;
    }
    
    if (adapter_id >= 256) {
        PyErr_SetString(PyExc_ValueError, "adapter_id must be < 256");
        return NULL;
    }
    
    g_state.resolvers[adapter_id] = (ResolverFn)fnptr;
    
    Py_RETURN_NONE;
}

/* Signal handler - ASYNC-SIGNAL-SAFE ONLY */
static void signal_handler(int sig, siginfo_t *si, void *unused) {
    (void)unused;  /* Context parameter - unused */
    
    if (sig != SIGSEGV || !g_state.ring) {
        return;
    }
    
    uintptr_t fault_addr = (uintptr_t)si->si_addr;
    uintptr_t page_start = (fault_addr / PAGE_SIZE) * PAGE_SIZE;
    
    /* Atomic ring enqueue - O(1) only */
    uint32_t seq = atomic_fetch_add(&g_state.seq_counter, 1);
    uint32_t head = atomic_load(&g_state.ring_head);
    uint32_t next_head = (head + 1) % RING_CAPACITY;
    
    if (next_head == atomic_load(&g_state.ring_tail)) {
        /* Ring full - drop event */
        atomic_fetch_add(&g_state.dropped_events, 1);
        return;
    }
    
    /* Write event */
    PageEvent *event = &g_state.ring[head];
    event->page_start = page_start;
    event->fault_ip = fault_addr;  /* Use actual fault address */
    event->adapter_id = 0;  /* resolved by worker */
    event->timestamp_ns = get_monotonic_ns();
    event->seq = seq;
    event->thread_id = (uint32_t)pthread_self();
    
    atomic_store(&g_state.ring_head, next_head);
    
    /* Temporarily allow write to this page */
    mprotect((void*)page_start, PAGE_SIZE, PROT_READ | PROT_WRITE);
}

/* Worker thread - drains ring and processes events */
static void *worker_thread_func(void *arg) {
    (void)arg;  /* Unused thread argument */
    
    uint8_t preview_buffer[PREVIEW_SIZE];
    
    while (!atomic_load(&g_state.shutdown_requested)) {
        uint32_t tail = atomic_load(&g_state.ring_tail);
        uint32_t head = atomic_load(&g_state.ring_head);
        
        if (tail == head) {
            /* Ring empty */
            usleep(1000);  /* 1ms */
            continue;
        }
        
        /* Process event */
        PageEvent event = g_state.ring[tail];
        atomic_store(&g_state.ring_tail, (tail + 1) % RING_CAPACITY);
        
        /* Find regions for this page */
        pthread_mutex_lock(&g_state.page_table_mutex);
        PageEntry *entry = page_table_find(event.page_start);
        
        if (!entry || !entry->regions) {
            pthread_mutex_unlock(&g_state.page_table_mutex);
            continue;
        }
        
        /* Process each region on this page */
        for (TrackedRegion *region = entry->regions; region; region = region->next_in_page) {
            /* Compute current hash */
            uint64_t current_hash = hash_bytes((void*)region->addr, region->size);
            
            if (current_hash != region->last_hash) {
                /* Change detected - build event */
                PyGILState_STATE gstate = PyGILState_Ensure();
                
                PyObject *event_dict = PyDict_New();
                
                /* Create and add seq */
                PyObject *seq_obj = PyLong_FromUnsignedLong(event.seq);
                PyDict_SetItemString(event_dict, "seq", seq_obj);
                Py_DECREF(seq_obj);
                
                /* Create and add timestamp */
                PyObject *ts_obj = PyLong_FromUnsignedLongLong(event.timestamp_ns);
                PyDict_SetItemString(event_dict, "timestamp_ns", ts_obj);
                Py_DECREF(ts_obj);
                
                /* Create and add adapter_id */
                PyObject *adapter_obj = PyLong_FromUnsignedLong(region->adapter_id);
                PyDict_SetItemString(event_dict, "adapter_id", adapter_obj);
                Py_DECREF(adapter_obj);
                
                /* Create and add region_id */
                PyObject *region_obj = PyLong_FromUnsignedLong(region->region_id);
                PyDict_SetItemString(event_dict, "region_id", region_obj);
                Py_DECREF(region_obj);
                
                /* Create and add how_big */
                PyObject *size_obj = PyLong_FromSize_t(region->size);
                PyDict_SetItemString(event_dict, "how_big", size_obj);
                Py_DECREF(size_obj);
                
                /* Add previews */
                size_t preview_len = (region->size < PREVIEW_SIZE) ? region->size : PREVIEW_SIZE;
                memcpy(preview_buffer, (void*)region->addr, preview_len);
                PyObject *preview_obj = PyBytes_FromStringAndSize((char*)preview_buffer, preview_len);
                PyDict_SetItemString(event_dict, "new_preview", preview_obj);
                Py_DECREF(preview_obj);
                
                /* For small regions, include full value */
                if (region->size <= SMALL_COPY_THRESHOLD) {
                    PyObject *value_obj = PyBytes_FromStringAndSize((char*)region->addr, region->size);
                    PyDict_SetItemString(event_dict, "new_value", value_obj);
                    Py_DECREF(value_obj);
                } else {
                    /* Large region - would use storage here */
                    char storage_key[256];
                    snprintf(storage_key, sizeof(storage_key), 
                            "memwatch/%u/%u/%u", region->adapter_id, region->region_id, region->epoch);
                    PyObject *storage_obj = PyUnicode_FromString(storage_key);
                    PyDict_SetItemString(event_dict, "storage_key_new", storage_obj);
                    Py_DECREF(storage_obj);
                }
                
                /* Add where info */
                PyObject *where = PyDict_New();
                char ip_str[32];
                snprintf(ip_str, sizeof(ip_str), "0x%lx", event.fault_ip);
                PyObject *ip_obj = PyUnicode_FromString(ip_str);
                PyDict_SetItemString(where, "fault_ip", ip_obj);
                Py_DECREF(ip_obj);
                
                PyDict_SetItemString(event_dict, "where", where);
                Py_DECREF(where);  /* Dict took a reference */
                
                /* Update region state */
                region->last_hash = current_hash;
                region->epoch++;
                
                /* Invoke callback */
                pthread_mutex_lock(&g_state.callback_mutex);
                if (g_state.callback) {
                    PyObject *result = PyObject_CallFunctionObjArgs(g_state.callback, event_dict, NULL);
                    Py_XDECREF(result);
                }
                pthread_mutex_unlock(&g_state.callback_mutex);
                
                Py_DECREF(event_dict);
                PyGILState_Release(gstate);
            }
        }
        
        pthread_mutex_unlock(&g_state.page_table_mutex);
        
        /* Re-protect page after writable window */
        if (g_state.protection_available) {
            usleep(WRITABLE_WINDOW_MS * 1000);
            mprotect((void*)event.page_start, PAGE_SIZE, PROT_READ);
        }
    }
    
    return NULL;
}

/* Hash function - FNV-1a */
static uint64_t hash_bytes(const void *data, size_t len) {
    const uint8_t *bytes = data;
    uint64_t hash = 14695981039346656037ULL;
    
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    
    return hash;
}

/* Get monotonic timestamp in nanoseconds */
static uint64_t get_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Page table functions */
static PageEntry *page_table_find(uintptr_t page_start) {
    size_t index = (page_start / PAGE_SIZE) % g_state.page_table_capacity;
    
    for (size_t i = 0; i < g_state.page_table_capacity; i++) {
        size_t idx = (index + i) % g_state.page_table_capacity;
        if (g_state.page_table[idx].page_start == page_start) {
            return &g_state.page_table[idx];
        }
        if (g_state.page_table[idx].page_start == 0) {
            return NULL;
        }
    }
    
    return NULL;
}

static void page_table_add_region(uintptr_t page_start, TrackedRegion *region) {
    pthread_mutex_lock(&g_state.page_table_mutex);
    
    PageEntry *entry = page_table_find(page_start);
    
    if (!entry) {
        /* Find empty slot */
        size_t index = (page_start / PAGE_SIZE) % g_state.page_table_capacity;
        for (size_t i = 0; i < g_state.page_table_capacity; i++) {
            size_t idx = (index + i) % g_state.page_table_capacity;
            if (g_state.page_table[idx].page_start == 0) {
                entry = &g_state.page_table[idx];
                entry->page_start = page_start;
                entry->regions = NULL;
                entry->region_count = 0;
                break;
            }
        }
    }
    
    if (entry) {
        /* Add to linked list */
        region->next_in_page = entry->regions;
        entry->regions = region;
        entry->region_count++;
    }
    
    pthread_mutex_unlock(&g_state.page_table_mutex);
}

static void page_table_remove_region(uintptr_t page_start, TrackedRegion *region) {
    pthread_mutex_lock(&g_state.page_table_mutex);
    
    PageEntry *entry = page_table_find(page_start);
    if (entry) {
        TrackedRegion **prev = &entry->regions;
        for (TrackedRegion *curr = entry->regions; curr; curr = curr->next_in_page) {
            if (curr == region) {
                *prev = curr->next_in_page;
                entry->region_count--;
                break;
            }
            prev = &curr->next_in_page;
        }
        
        /* If no more regions on this page, restore write permission */
        if (entry->region_count == 0 && g_state.protection_available) {
            mprotect((void*)page_start, PAGE_SIZE, PROT_READ | PROT_WRITE);
            entry->page_start = 0;
        }
    }
    
    pthread_mutex_unlock(&g_state.page_table_mutex);
}

/* Python module definition */
static PyMethodDef MemwatchMethods[] = {
    {"init", mw_init, METH_VARARGS, "Initialize memwatch core"},
    {"shutdown", mw_shutdown, METH_VARARGS, "Shutdown memwatch core"},
    {"track", mw_track, METH_VARARGS, "Track a memory region"},
    {"untrack", mw_untrack, METH_VARARGS, "Untrack a memory region"},
    {"set_callback", mw_set_callback, METH_VARARGS, "Set event callback"},
    {"get_stats", mw_get_stats, METH_VARARGS, "Get statistics"},
    {"register_resolver", mw_register_resolver, METH_VARARGS, "Register resolver function"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef memwatch_module = {
    PyModuleDef_HEAD_INIT,
    "_memwatch_native",  /* Module name changed to avoid collision */
    "Native memory change watcher core",
    -1,
    MemwatchMethods,
    NULL,  /* m_slots */
    NULL,  /* m_traverse */
    NULL,  /* m_clear */
    NULL   /* m_free */
};

PyMODINIT_FUNC PyInit__memwatch_native(void) {  /* Function name must match module name */
    return PyModule_Create(&memwatch_module);
}
