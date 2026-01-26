#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

// Dynamic growth factor
#define GROWTH_FACTOR 2
#define INITIAL_CAPACITY 64

// Structure to hold tracked memory region metadata
typedef struct TrackedRegion {
    void *page_addr;           // Page-aligned address
    void *actual_addr;         // Actual buffer address
    size_t size;               // Size of tracked region
    char *tag;                 // Variable/object name (dynamic)
    PyObject *py_obj;          // Python object reference
    void *old_value;           // Complete old value (dynamic)
    size_t old_value_size;     // Size of old value
    struct TrackedRegion *next; // For hash table collision chaining
} TrackedRegion;

// Hash table for O(1) lookups
typedef struct {
    TrackedRegion **buckets;
    size_t capacity;
    size_t count;
    pthread_mutex_t lock;
} RegionTable;

// Configuration
typedef struct {
    size_t max_memory_bytes;   // User-configured memory limit
    size_t current_memory;     // Current memory usage
    int capture_full_values;   // Capture complete values or minimal
} MemWatchConfig;

// Global state
static RegionTable *region_table = NULL;
static PyObject *change_callback = NULL;
static struct sigaction old_action;
static int initialized = 0;
static MemWatchConfig config = {
    .max_memory_bytes = 1024 * 1024 * 1024,  // 1GB default
    .current_memory = 0,
    .capture_full_values = 1
};

// Thread-safe memory tracking
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;

// ==============================================================================
// Memory Management Functions
// ==============================================================================

static size_t calculate_memory_usage(TrackedRegion *region) {
    size_t usage = sizeof(TrackedRegion);
    if (region->tag) usage += strlen(region->tag) + 1;
    if (region->old_value) usage += region->old_value_size;
    return usage;
}

static int check_memory_limit(size_t additional_bytes) {
    pthread_mutex_lock(&memory_lock);
    int result = (config.current_memory + additional_bytes) <= config.max_memory_bytes;
    pthread_mutex_unlock(&memory_lock);
    return result;
}

static void update_memory_usage(ssize_t delta) {
    pthread_mutex_lock(&memory_lock);
    if (delta > 0) {
        config.current_memory += delta;
    } else {
        config.current_memory -= (-delta);
    }
    pthread_mutex_unlock(&memory_lock);
}

// ==============================================================================
// Hash Table Functions
// ==============================================================================

static size_t hash_address(void *addr, size_t capacity) {
    uintptr_t val = (uintptr_t)addr;
    val = ((val >> 16) ^ val) * 0x45d9f3b;
    val = ((val >> 16) ^ val) * 0x45d9f3b;
    val = (val >> 16) ^ val;
    return val % capacity;
}

static RegionTable* create_region_table(size_t initial_capacity) {
    RegionTable *table = malloc(sizeof(RegionTable));
    if (!table) return NULL;
    
    table->buckets = calloc(initial_capacity, sizeof(TrackedRegion*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }
    
    table->capacity = initial_capacity;
    table->count = 0;
    pthread_mutex_init(&table->lock, NULL);
    
    return table;
}

static void free_region(TrackedRegion *region) {
    if (!region) return;
    
    size_t mem_used = calculate_memory_usage(region);
    
    if (region->tag) free(region->tag);
    if (region->old_value) free(region->old_value);
    if (region->py_obj) {
        Py_DECREF(region->py_obj);
    }
    
    free(region);
    update_memory_usage(-mem_used);
}

static void resize_table(RegionTable *table) {
    size_t new_capacity = table->capacity * GROWTH_FACTOR;
    TrackedRegion **new_buckets = calloc(new_capacity, sizeof(TrackedRegion*));
    
    if (!new_buckets) return; // Keep old table if allocation fails
    
    // Rehash all entries
    for (size_t i = 0; i < table->capacity; i++) {
        TrackedRegion *region = table->buckets[i];
        while (region) {
            TrackedRegion *next = region->next;
            size_t new_idx = hash_address(region->actual_addr, new_capacity);
            region->next = new_buckets[new_idx];
            new_buckets[new_idx] = region;
            region = next;
        }
    }
    
    free(table->buckets);
    table->buckets = new_buckets;
    table->capacity = new_capacity;
}

static int insert_region(RegionTable *table, TrackedRegion *region) {
    pthread_mutex_lock(&table->lock);
    
    // Resize if load factor > 0.75
    if (table->count * 4 > table->capacity * 3) {
        resize_table(table);
    }
    
    size_t idx = hash_address(region->actual_addr, table->capacity);
    region->next = table->buckets[idx];
    table->buckets[idx] = region;
    table->count++;
    
    pthread_mutex_unlock(&table->lock);
    return 0;
}

static TrackedRegion* find_region_by_addr(RegionTable *table, void *fault_addr) {
    if (!table) return NULL;
    
    pthread_mutex_lock(&table->lock);
    
    // Check all buckets (we need to check ranges, not exact addresses)
    for (size_t i = 0; i < table->capacity; i++) {
        TrackedRegion *region = table->buckets[i];
        while (region) {
            uintptr_t fault = (uintptr_t)fault_addr;
            uintptr_t start = (uintptr_t)region->actual_addr;
            uintptr_t end = start + region->size;
            
            if (fault >= start && fault < end) {
                pthread_mutex_unlock(&table->lock);
                return region;
            }
            region = region->next;
        }
    }
    
    pthread_mutex_unlock(&table->lock);
    return NULL;
}

static int remove_region(RegionTable *table, void *addr) {
    pthread_mutex_lock(&table->lock);
    
    for (size_t i = 0; i < table->capacity; i++) {
        TrackedRegion **prev = &table->buckets[i];
        TrackedRegion *region = table->buckets[i];
        
        while (region) {
            if (region->actual_addr == addr) {
                *prev = region->next;
                table->count--;
                pthread_mutex_unlock(&table->lock);
                free_region(region);
                return 0;
            }
            prev = &region->next;
            region = region->next;
        }
    }
    
    pthread_mutex_unlock(&table->lock);
    return -1;
}

// ==============================================================================
// Page Protection Functions
// ==============================================================================

static size_t get_page_size(void) {
    static size_t page_size = 0;
    if (page_size == 0) {
        page_size = sysconf(_SC_PAGESIZE);
    }
    return page_size;
}

static void* align_to_page(void *addr) {
    size_t page_size = get_page_size();
    uintptr_t aligned = ((uintptr_t)addr) & ~(page_size - 1);
    return (void*)aligned;
}

static size_t calculate_page_count(void *addr, size_t size) {
    size_t page_size = get_page_size();
    void *page_start = align_to_page(addr);
    uintptr_t end = (uintptr_t)addr + size;
    uintptr_t page_end = (((uintptr_t)end + page_size - 1) / page_size) * page_size;
    return (page_end - (uintptr_t)page_start) / page_size;
}

// ==============================================================================
// Signal Handler
// ==============================================================================

static void segv_handler(int sig, siginfo_t *info, void *context) {
    void *fault_addr = info->si_addr;
    TrackedRegion *region = find_region_by_addr(region_table, fault_addr);
    
    if (region == NULL) {
        // Not our fault, restore old handler and re-raise
        sigaction(SIGSEGV, &old_action, NULL);
        raise(SIGSEGV);
        return;
    }
    
    // Temporarily allow write access
    size_t page_size = get_page_size();
    size_t num_pages = calculate_page_count(region->actual_addr, region->size);
    mprotect(region->page_addr, num_pages * page_size, PROT_READ | PROT_WRITE);
    
    // The write will now complete successfully
    // We'll need to capture the change and re-protect in the Python callback
}

static int init_signal_handler(void) {
    if (initialized) return 0;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    
    if (sigaction(SIGSEGV, &sa, &old_action) != 0) {
        return -1;
    }
    
    initialized = 1;
    return 0;
}

// ==============================================================================
// Python Interface Functions
// ==============================================================================

static PyObject* py_configure(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"max_memory_mb", "capture_full_values", NULL};
    unsigned long max_memory_mb = 0;
    int capture_full = -1;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|kp", kwlist, 
                                     &max_memory_mb, &capture_full)) {
        return NULL;
    }
    
    if (max_memory_mb > 0) {
        config.max_memory_bytes = max_memory_mb * 1024 * 1024;
    }
    
    if (capture_full >= 0) {
        config.capture_full_values = capture_full;
    }
    
    Py_RETURN_NONE;
}

static PyObject* py_track(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"buffer", "size", "tag", "py_obj", NULL};
    unsigned long long buffer_addr;
    Py_ssize_t size;
    const char *tag;
    PyObject *py_obj = Py_None;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Kns|O", kwlist,
                                     &buffer_addr, &size, &tag, &py_obj)) {
        return NULL;
    }
    
    if (size <= 0) {
        PyErr_SetString(PyExc_ValueError, "Size must be positive");
        return NULL;
    }
    
    void *addr = (void*)buffer_addr;
    
    // Calculate memory needed
    size_t mem_needed = sizeof(TrackedRegion) + strlen(tag) + 1;
    if (config.capture_full_values) {
        mem_needed += size;
    }
    
    if (!check_memory_limit(mem_needed)) {
        PyErr_SetString(PyExc_MemoryError, "Memory limit exceeded for tracking");
        return NULL;
    }
    
    // Initialize if needed
    if (!region_table) {
        region_table = create_region_table(INITIAL_CAPACITY);
        if (!region_table) {
            PyErr_SetString(PyExc_MemoryError, "Failed to initialize region table");
            return NULL;
        }
    }
    
    if (init_signal_handler() != 0) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to initialize signal handler");
        return NULL;
    }
    
    // Create tracked region
    TrackedRegion *region = malloc(sizeof(TrackedRegion));
    if (!region) {
        PyErr_NoMemory();
        return NULL;
    }
    
    memset(region, 0, sizeof(TrackedRegion));
    region->actual_addr = addr;
    region->size = size;
    region->page_addr = align_to_page(addr);
    
    // Copy tag
    region->tag = strdup(tag);
    if (!region->tag) {
        free(region);
        PyErr_NoMemory();
        return NULL;
    }
    
    // Store Python object reference
    if (py_obj != Py_None) {
        Py_INCREF(py_obj);
        region->py_obj = py_obj;
    }
    
    // Capture old value
    if (config.capture_full_values) {
        region->old_value = malloc(size);
        if (region->old_value) {
            memcpy(region->old_value, addr, size);
            region->old_value_size = size;
        }
    }
    
    // Protect memory
    size_t page_size = get_page_size();
    size_t num_pages = calculate_page_count(addr, size);
    
    if (mprotect(region->page_addr, num_pages * page_size, PROT_READ) != 0) {
        free_region(region);
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    
    // Insert into table
    insert_region(region_table, region);
    update_memory_usage(mem_needed);
    
    Py_RETURN_NONE;
}

static PyObject* py_untrack(PyObject *self, PyObject *args) {
    unsigned long long buffer_addr;
    
    if (!PyArg_ParseTuple(args, "K", &buffer_addr)) {
        return NULL;
    }
    
    void *addr = (void*)buffer_addr;
    TrackedRegion *region = find_region_by_addr(region_table, addr);
    
    if (!region) {
        PyErr_SetString(PyExc_ValueError, "Address not tracked");
        return NULL;
    }
    
    // Restore write permissions
    size_t page_size = get_page_size();
    size_t num_pages = calculate_page_count(region->actual_addr, region->size);
    mprotect(region->page_addr, num_pages * page_size, PROT_READ | PROT_WRITE);
    
    // Remove from table
    remove_region(region_table, addr);
    
    Py_RETURN_NONE;
}

static PyObject* py_check_and_reprotect(PyObject *self, PyObject *args) {
    unsigned long long buffer_addr;
    
    if (!PyArg_ParseTuple(args, "K", &buffer_addr)) {
        return NULL;
    }
    
    void *addr = (void*)buffer_addr;
    TrackedRegion *region = find_region_by_addr(region_table, addr);
    
    if (!region) {
        Py_RETURN_NONE;
    }
    
    // Check if value changed
    int changed = 0;
    PyObject *old_val = Py_None;
    PyObject *new_val = Py_None;
    
    if (config.capture_full_values && region->old_value) {
        if (memcmp(region->old_value, region->actual_addr, region->size) != 0) {
            changed = 1;
            
            // Create bytes objects for old and new values
            old_val = PyBytes_FromStringAndSize(region->old_value, region->old_value_size);
            new_val = PyBytes_FromStringAndSize(region->actual_addr, region->size);
            
            // Update old value
            memcpy(region->old_value, region->actual_addr, region->size);
        }
    }
    
    // Re-protect memory
    size_t page_size = get_page_size();
    size_t num_pages = calculate_page_count(region->actual_addr, region->size);
    mprotect(region->page_addr, num_pages * page_size, PROT_READ);
    
    if (changed && change_callback) {
        PyObject *result = PyObject_CallFunction(change_callback, "sOO", 
                                                 region->tag, old_val, new_val);
        Py_XDECREF(result);
        Py_XDECREF(old_val);
        Py_XDECREF(new_val);
    }
    
    Py_RETURN_NONE;
}

static PyObject* py_set_callback(PyObject *self, PyObject *args) {
    PyObject *callback;
    
    if (!PyArg_ParseTuple(args, "O", &callback)) {
        return NULL;
    }
    
    if (!PyCallable_Check(callback) && callback != Py_None) {
        PyErr_SetString(PyExc_TypeError, "Callback must be callable");
        return NULL;
    }
    
    Py_XDECREF(change_callback);
    
    if (callback == Py_None) {
        change_callback = NULL;
    } else {
        Py_INCREF(callback);
        change_callback = callback;
    }
    
    Py_RETURN_NONE;
}

static PyObject* py_get_stats(PyObject *self, PyObject *args) {
    PyObject *dict = PyDict_New();
    
    PyDict_SetItemString(dict, "tracked_regions", 
                        PyLong_FromSize_t(region_table ? region_table->count : 0));
    PyDict_SetItemString(dict, "memory_used_bytes", 
                        PyLong_FromSize_t(config.current_memory));
    PyDict_SetItemString(dict, "memory_limit_bytes", 
                        PyLong_FromSize_t(config.max_memory_bytes));
    PyDict_SetItemString(dict, "capture_full_values", 
                        PyBool_FromLong(config.capture_full_values));
    
    return dict;
}

// ==============================================================================
// Module Definition
// ==============================================================================

static PyMethodDef memwatch_methods[] = {
    {"configure", (PyCFunction)py_configure, METH_VARARGS | METH_KEYWORDS,
     "Configure memory tracking limits"},
    {"track", (PyCFunction)py_track, METH_VARARGS | METH_KEYWORDS,
     "Track a memory region"},
    {"untrack", py_untrack, METH_VARARGS,
     "Stop tracking a memory region"},
    {"check_and_reprotect", py_check_and_reprotect, METH_VARARGS,
     "Check for changes and re-protect memory"},
    {"set_callback", py_set_callback, METH_VARARGS,
     "Set the change notification callback"},
    {"get_stats", py_get_stats, METH_NOARGS,
     "Get tracking statistics"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef memwatch_module = {
    PyModuleDef_HEAD_INIT,
    "memwatch",
    "Hardware-assisted memory change tracking for Python",
    -1,
    memwatch_methods
};

PyMODINIT_FUNC PyInit_memwatch(void) {
    return PyModule_Create(&memwatch_module);
}