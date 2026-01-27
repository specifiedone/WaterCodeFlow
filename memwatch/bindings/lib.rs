// Rust binding for memwatch
// Place in: rust/src/lib.rs

use std::collections::HashMap;
use std::ffi::CString;
use std::os::raw::{c_char, c_void, c_int};
use std::ptr;
use std::sync::Mutex;

#[repr(C)]
pub struct ChangeEventC {
    pub seq: u32,
    pub timestamp_ns: u64,
    pub adapter_id: u32,
    pub region_id: u32,
    pub variable_name: *const c_char,
    pub file: *const c_char,
    pub function: *const c_char,
    pub line: u32,
    pub fault_ip: u64,
    pub old_preview: *const u8,
    pub old_preview_size: usize,
    pub new_preview: *const u8,
    pub new_preview_size: usize,
}

#[repr(C)]
pub struct StatsC {
    pub num_tracked_regions: u32,
    pub num_active_watchpoints: u32,
    pub total_events: u64,
    pub ring_write_count: u64,
    pub ring_drop_count: u64,
    pub storage_bytes_used: u64,
    pub mprotect_page_count: u32,
    pub worker_thread_id: u32,
    pub worker_cycles: u64,
}

// C function bindings
extern "C" {
    fn memwatch_init() -> c_int;
    fn memwatch_shutdown();
    fn memwatch_watch(addr: u64, size: usize, name: *const c_char, user_data: *mut c_void) -> u32;
    fn memwatch_unwatch(region_id: u32) -> bool;
    fn memwatch_set_callback(callback: *mut c_void, user_ctx: *mut c_void) -> c_int;
    fn memwatch_check_changes(out_events: *mut ChangeEventC, max_events: c_int) -> c_int;
    fn memwatch_get_stats(out_stats: *mut StatsC) -> c_int;
    fn memwatch_free_event(event: *mut ChangeEventC);
}

/// Change event - unified across all languages
#[derive(Debug, Clone)]
pub struct ChangeEvent {
    pub seq: u32,
    pub timestamp_ns: u64,
    pub adapter_id: u32,
    pub region_id: u32,
    pub variable_name: Option<String>,
    pub where_: Location,
    pub old_preview: Vec<u8>,
    pub new_preview: Vec<u8>,
    pub old_value: Vec<u8>,
    pub new_value: Vec<u8>,
    pub storage_key_old: Option<String>,
    pub storage_key_new: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Location {
    pub file: Option<String>,
    pub function: Option<String>,
    pub line: u32,
    pub fault_ip: u64,
}

/// Statistics
#[derive(Debug, Clone)]
pub struct Stats {
    pub num_tracked_regions: u32,
    pub num_active_watchpoints: u32,
    pub total_events: u64,
    pub ring_write_count: u64,
    pub ring_drop_count: u64,
    pub storage_bytes_used: u64,
    pub mprotect_page_count: u32,
    pub worker_thread_id: u32,
    pub worker_cycles: u64,
}

/// Callback function type
pub type ChangeEventCallback = Box<dyn Fn(&ChangeEvent) + Send>;

/// Memory watcher - unified API for Rust
pub struct MemWatch {
    tracked_objects: Mutex<HashMap<u32, Box<dyn std::any::Any>>>,
    callback: Mutex<Option<ChangeEventCallback>>,
}

impl MemWatch {
    /// Create a new memory watcher
    pub fn new() -> Result<Self, String> {
        unsafe {
            let result = memwatch_init();
            if result != 0 {
                return Err(format!("Failed to initialize memwatch: {}", result));
            }
        }
        
        Ok(MemWatch {
            tracked_objects: Mutex::new(HashMap::new()),
            callback: Mutex::new(None),
        })
    }
    
    /// Watch a buffer for changes
    pub fn watch(&self, buffer: &[u8], name: &str) -> Result<u32, String> {
        let addr = buffer.as_ptr() as u64;
        let size = buffer.len();
        let c_name = CString::new(name).map_err(|e| e.to_string())?;
        
        unsafe {
            let region_id = memwatch_watch(addr, size, c_name.as_ptr(), ptr::null_mut());
            if region_id > 0 {
                Ok(region_id)
            } else {
                Err("Failed to watch buffer".to_string())
            }
        }
    }
    
    /// Watch a vector for changes
    pub fn watch_vec<T>(&self, vec: &[T], name: &str) -> Result<u32, String> {
        let addr = vec.as_ptr() as u64;
        let size = vec.len() * std::mem::size_of::<T>();
        let c_name = CString::new(name).map_err(|e| e.to_string())?;
        
        unsafe {
            let region_id = memwatch_watch(addr, size, c_name.as_ptr(), ptr::null_mut());
            if region_id > 0 {
                Ok(region_id)
            } else {
                Err("Failed to watch vector".to_string())
            }
        }
    }
    
    /// Stop watching a region
    pub fn unwatch(&self, region_id: u32) -> bool {
        unsafe {
            memwatch_unwatch(region_id) && 
            self.tracked_objects.lock().unwrap().remove(&region_id).is_some()
        }
    }
    
    /// Set callback for change events
    pub fn set_callback<F>(&self, callback: Option<F>) -> Result<(), String>
    where
        F: Fn(&ChangeEvent) + Send + 'static,
    {
        if let Some(cb) = callback {
            let boxed = Box::new(cb);
            *self.callback.lock().unwrap() = Some(boxed);
            
            unsafe {
                let result = memwatch_set_callback(ptr::null_mut(), ptr::null_mut());
                if result != 0 {
                    return Err(format!("Failed to set callback: {}", result));
                }
            }
        } else {
            *self.callback.lock().unwrap() = None;
            unsafe {
                memwatch_set_callback(ptr::null_mut(), ptr::null_mut());
            }
        }
        
        Ok(())
    }
    
    /// Synchronously check for changes (polling mode)
    pub fn check_changes(&self) -> Result<Vec<ChangeEvent>, String> {
        const MAX_EVENTS: usize = 16;
        let mut c_events = vec![
            ChangeEventC {
                seq: 0,
                timestamp_ns: 0,
                adapter_id: 0,
                region_id: 0,
                variable_name: ptr::null(),
                file: ptr::null(),
                function: ptr::null(),
                line: 0,
                fault_ip: 0,
                old_preview: ptr::null(),
                old_preview_size: 0,
                new_preview: ptr::null(),
                new_preview_size: 0,
            };
            MAX_EVENTS
        ];
        
        unsafe {
            let count = memwatch_check_changes(c_events.as_mut_ptr(), MAX_EVENTS as c_int);
            
            let mut result = Vec::with_capacity(count as usize);
            for i in 0..(count as usize) {
                let c_evt = &c_events[i];
                
                result.push(ChangeEvent {
                    seq: c_evt.seq,
                    timestamp_ns: c_evt.timestamp_ns,
                    adapter_id: c_evt.adapter_id,
                    region_id: c_evt.region_id,
                    variable_name: if c_evt.variable_name.is_null() {
                        None
                    } else {
                        Some(CString::from_raw(c_evt.variable_name as *mut c_char).into_string().unwrap_or_default())
                    },
                    where_: Location {
                        file: if c_evt.file.is_null() {
                            None
                        } else {
                            Some(CString::from_raw(c_evt.file as *mut c_char).into_string().unwrap_or_default())
                        },
                        function: if c_evt.function.is_null() {
                            None
                        } else {
                            Some(CString::from_raw(c_evt.function as *mut c_char).into_string().unwrap_or_default())
                        },
                        line: c_evt.line,
                        fault_ip: c_evt.fault_ip,
                    },
                    old_preview: if c_evt.old_preview.is_null() {
                        Vec::new()
                    } else {
                        std::slice::from_raw_parts(c_evt.old_preview, c_evt.old_preview_size).to_vec()
                    },
                    new_preview: if c_evt.new_preview.is_null() {
                        Vec::new()
                    } else {
                        std::slice::from_raw_parts(c_evt.new_preview, c_evt.new_preview_size).to_vec()
                    },
                    old_value: Vec::new(),
                    new_value: Vec::new(),
                    storage_key_old: None,
                    storage_key_new: None,
                });
                
                memwatch_free_event(&mut c_events[i]);
            }
            
            Ok(result)
        }
    }
    
    /// Get statistics
    pub fn get_stats(&self) -> Result<Stats, String> {
        unsafe {
            let mut c_stats = std::mem::zeroed::<StatsC>();
            let result = memwatch_get_stats(&mut c_stats);
            
            if result != 0 {
                return Err(format!("Failed to get stats: {}", result));
            }
            
            Ok(Stats {
                num_tracked_regions: c_stats.num_tracked_regions,
                num_active_watchpoints: c_stats.num_active_watchpoints,
                total_events: c_stats.total_events,
                ring_write_count: c_stats.ring_write_count,
                ring_drop_count: c_stats.ring_drop_count,
                storage_bytes_used: c_stats.storage_bytes_used,
                mprotect_page_count: c_stats.mprotect_page_count,
                worker_thread_id: c_stats.worker_thread_id,
                worker_cycles: c_stats.worker_cycles,
            })
        }
    }
}

impl Drop for MemWatch {
    fn drop(&mut self) {
        unsafe {
            memwatch_shutdown();
        }
    }
}

impl Default for MemWatch {
    fn default() -> Self {
        Self::new().expect("Failed to initialize MemWatch")
    }
}
