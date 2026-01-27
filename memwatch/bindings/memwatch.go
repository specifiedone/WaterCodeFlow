// Go binding for memwatch
// Place in: go/memwatch.go

package memwatch

/*
#cgo CFLAGS: -I../include
#cgo LDFLAGS: -L../build -lmemwatch_core

#include <memwatch_unified.h>
#include <stdint.h>
#include <stdlib.h>
*/
import "C"
import (
    "fmt"
    "unsafe"
)

// ChangeEvent - unified event structure
type ChangeEvent struct {
    Seq             uint32
    TimestampNs     uint64
    AdapterID       uint32
    RegionID        uint32
    VariableName    string
    Where           Location
    OldPreview      []byte
    NewPreview      []byte
    OldValue        []byte
    NewValue        []byte
    StorageKeyOld   string
    StorageKeyNew   string
    Metadata        map[string]interface{}
}

// Location - where the change occurred
type Location struct {
    File     string
    Function string
    Line     uint32
    FaultIP  uint64
}

// Stats - statistics
type Stats struct {
    NumTrackedRegions     uint32
    NumActiveWatchpoints  uint32
    TotalEvents           uint64
    RingWriteCount        uint64
    RingDropCount         uint64
    StorageBytesUsed      uint64
    MprotectPageCount     uint32
    WorkerThreadID        uint32
    WorkerCycles          uint64
}

// ChangeEventCallback - callback function type
type ChangeEventCallback func(*ChangeEvent)

var (
    globalCallback ChangeEventCallback
)

// MemWatch - the main watcher struct
type MemWatch struct {
    trackedObjects map[uint32]interface{}
    callback       ChangeEventCallback
}

// NewWatcher creates a new memory watcher
func NewWatcher() (*MemWatch, error) {
    result := C.memwatch_init()
    if result != 0 {
        return nil, fmt.Errorf("failed to initialize memwatch: %d", result)
    }
    
    return &MemWatch{
        trackedObjects: make(map[uint32]interface{}),
    }, nil
}

// Watch starts watching a memory region
// addr: memory address
// size: size in bytes
// name: variable name
// Returns region_id
func (w *MemWatch) Watch(data interface{}, name string) (uint32, error) {
    var addr uintptr
    var size int
    
    switch v := data.(type) {
    case []byte:
        if len(v) == 0 {
            return 0, fmt.Errorf("cannot watch empty slice")
        }
        addr = uintptr(unsafe.Pointer(&v[0]))
        size = len(v)
    case []int:
        if len(v) == 0 {
            return 0, fmt.Errorf("cannot watch empty slice")
        }
        addr = uintptr(unsafe.Pointer(&v[0]))
        size = len(v) * 8 // int is typically 8 bytes
    default:
        return 0, fmt.Errorf("unsupported type: %T", v)
    }
    
    c_name := C.CString(name)
    defer C.free(unsafe.Pointer(c_name))
    
    region_id := C.memwatch_watch(C.uint64_t(addr), C.size_t(size), c_name, nil)
    
    if region_id > 0 {
        w.trackedObjects[uint32(region_id)] = data
    }
    
    return uint32(region_id), nil
}

// Unwatch stops watching a region
func (w *MemWatch) Unwatch(region_id uint32) bool {
    result := C.memwatch_unwatch(C.memwatch_region_id(region_id))
    if result {
        delete(w.trackedObjects, region_id)
    }
    return bool(result)
}

// SetCallback sets the change event callback
func (w *MemWatch) SetCallback(callback ChangeEventCallback) error {
    w.callback = callback
    globalCallback = callback
    
    if callback != nil {
        // We would need to use cgo callback mechanism here
        // This is a simplified version
        result := C.memwatch_set_callback(nil, nil)
        if result != 0 {
            return fmt.Errorf("failed to set callback: %d", result)
        }
    } else {
        C.memwatch_set_callback(nil, nil)
    }
    
    return nil
}

// CheckChanges synchronously checks for changes (polling mode)
func (w *MemWatch) CheckChanges() ([]*ChangeEvent, error) {
    const maxEvents = 16
    events := make([]C.memwatch_change_event_t, maxEvents)
    
    count := C.memwatch_check_changes(&events[0], C.int(maxEvents))
    
    result := make([]*ChangeEvent, 0, int(count))
    
    for i := 0; i < int(count); i++ {
        evt := &events[i]
        changeEvent := &ChangeEvent{
            Seq:          uint32(evt.seq),
            TimestampNs:  uint64(evt.timestamp_ns),
            AdapterID:    uint32(evt.adapter_id),
            RegionID:     uint32(evt.region_id),
            VariableName: C.GoString(evt.variable_name),
            Where: Location{
                File:     C.GoString(evt.file),
                Function: C.GoString(evt.function),
                Line:     uint32(evt.line),
                FaultIP:  uint64(evt.fault_ip),
            },
            Metadata: make(map[string]interface{}),
        }
        
        if evt.old_preview_size > 0 && evt.old_preview != nil {
            changeEvent.OldPreview = C.GoBytes(unsafe.Pointer(evt.old_preview), C.int(evt.old_preview_size))
        }
        
        if evt.new_preview_size > 0 && evt.new_preview != nil {
            changeEvent.NewPreview = C.GoBytes(unsafe.Pointer(evt.new_preview), C.int(evt.new_preview_size))
        }
        
        C.memwatch_free_event(evt)
        result = append(result, changeEvent)
    }
    
    return result, nil
}

// GetStats returns current statistics
func (w *MemWatch) GetStats() (*Stats, error) {
    var c_stats C.memwatch_stats_t
    result := C.memwatch_get_stats(&c_stats)
    
    if result != 0 {
        return nil, fmt.Errorf("failed to get stats: %d", result)
    }
    
    return &Stats{
        NumTrackedRegions:    uint32(c_stats.num_tracked_regions),
        NumActiveWatchpoints: uint32(c_stats.num_active_watchpoints),
        TotalEvents:          uint64(c_stats.total_events),
        RingWriteCount:       uint64(c_stats.ring_write_count),
        RingDropCount:        uint64(c_stats.ring_drop_count),
        StorageBytesUsed:     uint64(c_stats.storage_bytes_used),
        MprotectPageCount:    uint32(c_stats.mprotect_page_count),
        WorkerThreadID:       uint32(c_stats.worker_thread_id),
        WorkerCycles:         uint64(c_stats.worker_cycles),
    }, nil
}

// Close shuts down the watcher
func (w *MemWatch) Close() {
    C.memwatch_shutdown()
}

// Legacy functions for backwards compatibility
func Init() error {
    result := C.memwatch_init()
    if result != 0 {
        return fmt.Errorf("failed to initialize: %d", result)
    }
    return nil
}

func Shutdown() {
    C.memwatch_shutdown()
}
