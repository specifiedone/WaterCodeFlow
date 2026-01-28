/// Universal SQL Tracker for Rust
/// Track SQL column-level changes across all databases

use libc::c_int;
use std::ffi::{CString, CStr};
use std::ptr::null_mut;

// SQL operation types
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum SQLOperation {
    Unknown = 0,
    Insert = 1,
    Update = 2,
    Delete = 3,
    Select = 4,
}

impl SQLOperation {
    pub fn as_str(&self) -> &'static str {
        match self {
            SQLOperation::Insert => "INSERT",
            SQLOperation::Update => "UPDATE",
            SQLOperation::Delete => "DELETE",
            SQLOperation::Select => "SELECT",
            _ => "UNKNOWN",
        }
    }
}

// FFI declarations for native library
#[link(name = "sql_tracker")]
extern "C" {
    pub fn sql_tracker_init(storage_path: *const i8) -> *mut std::ffi::c_void;
    pub fn sql_tracker_track_query(
        tracker: *mut std::ffi::c_void,
        query: *const i8,
        rows_affected: c_int,
        database: *const i8,
        old_value: *const i8,
        new_value: *const i8,
    ) -> c_int;
    pub fn sql_tracker_free(tracker: *mut std::ffi::c_void);
}

/// Single column change from SQL operation
#[derive(Debug, Clone)]
pub struct SQLChange {
    pub timestamp_ns: u64,
    pub table_name: String,
    pub column_name: String,
    pub operation: SQLOperation,
    pub old_value: Option<String>,
    pub new_value: Option<String>,
    pub rows_affected: i32,
    pub database: Option<String>,
    pub full_query: String,
}

impl SQLChange {
    pub fn to_dict(&self) -> std::collections::HashMap<String, String> {
        let mut map = std::collections::HashMap::new();
        map.insert("timestamp_ns".to_string(), self.timestamp_ns.to_string());
        map.insert("table_name".to_string(), self.table_name.clone());
        map.insert("column_name".to_string(), self.column_name.clone());
        map.insert("operation".to_string(), self.operation.as_str().to_string());
        if let Some(ref old) = self.old_value {
            map.insert("old_value".to_string(), old.clone());
        }
        if let Some(ref new) = self.new_value {
            map.insert("new_value".to_string(), new.clone());
        }
        map.insert("rows_affected".to_string(), self.rows_affected.to_string());
        if let Some(ref db) = self.database {
            map.insert("database".to_string(), db.clone());
        }
        map.insert("full_query".to_string(), self.full_query.clone());
        map
    }
}

/// SQL Tracker instance
pub struct SQLTracker {
    tracker: *mut std::ffi::c_void,
    storage_path: Option<String>,
    changes: Vec<SQLChange>,
}

impl SQLTracker {
    /// Create new tracker
    pub fn new(storage_path: Option<&str>) -> Self {
        unsafe {
            let path_c = storage_path.map(|p| CString::new(p).unwrap());
            let path_ptr = path_c.as_ref().map(|p| p.as_ptr()).unwrap_or(std::ptr::null());
            
            let tracker = sql_tracker_init(path_ptr);
            
            SQLTracker {
                tracker,
                storage_path: storage_path.map(|s| s.to_string()),
                changes: Vec::new(),
            }
        }
    }
    
    /// Track a SQL query
    pub fn track_query(
        &mut self,
        query: &str,
        rows_affected: i32,
        database: Option<&str>,
        old_value: Option<&str>,
        new_value: Option<&str>,
    ) -> i32 {
        unsafe {
            let query_c = CString::new(query).unwrap();
            let db_c = database.map(|d| CString::new(d).unwrap());
            let old_c = old_value.map(|o| CString::new(o).unwrap());
            let new_c = new_value.map(|n| CString::new(n).unwrap());
            
            sql_tracker_track_query(
                self.tracker,
                query_c.as_ptr(),
                rows_affected,
                db_c.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
                old_c.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
                new_c.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
            )
        }
    }
    
    /// Get changes with optional filters
    pub fn get_changes(
        &self,
        table_filter: Option<&str>,
        column_filter: Option<&str>,
        operation_filter: Option<&str>,
    ) -> Vec<SQLChange> {
        self.changes
            .iter()
            .filter(|change| {
                if let Some(table) = table_filter {
                    if change.table_name != table {
                        return false;
                    }
                }
                if let Some(column) = column_filter {
                    if change.column_name != column {
                        return false;
                    }
                }
                if let Some(op) = operation_filter {
                    if change.operation.as_str() != op {
                        return false;
                    }
                }
                true
            })
            .cloned()
            .collect()
    }
    
    /// Get all changes
    pub fn all_changes(&self) -> &[SQLChange] {
        &self.changes
    }
    
    /// Get summary statistics
    pub fn summary(&self) -> Summary {
        let mut summary = Summary::default();
        
        for change in &self.changes {
            summary.total_changes += 1;
            
            match change.operation {
                SQLOperation::Insert => summary.insert_count += 1,
                SQLOperation::Update => summary.update_count += 1,
                SQLOperation::Delete => summary.delete_count += 1,
                SQLOperation::Select => summary.select_count += 1,
                _ => {}
            }
            
            summary.tables.entry(change.table_name.clone())
                .and_modify(|e| *e += 1)
                .or_insert(1);
            
            summary.columns.insert(format!("{}.{}", change.table_name, change.column_name));
        }
        
        summary
    }
}

impl Drop for SQLTracker {
    fn drop(&mut self) {
        unsafe {
            if !self.tracker.is_null() {
                sql_tracker_free(self.tracker);
            }
        }
    }
}

/// Summary statistics
#[derive(Debug, Default)]
pub struct Summary {
    pub total_changes: usize,
    pub insert_count: usize,
    pub update_count: usize,
    pub delete_count: usize,
    pub select_count: usize,
    pub tables: std::collections::HashMap<String, usize>,
    pub columns: std::collections::HashSet<String>,
}

// Global tracker
static mut GLOBAL_TRACKER: Option<SQLTracker> = None;

/// Initialize global tracker
pub fn init(storage_path: Option<&str>) -> &'static mut SQLTracker {
    unsafe {
        if GLOBAL_TRACKER.is_some() {
            GLOBAL_TRACKER = None;
        }
        GLOBAL_TRACKER = Some(SQLTracker::new(storage_path));
        GLOBAL_TRACKER.as_mut().unwrap()
    }
}

/// Get global tracker (must be initialized first)
pub fn get() -> &'static mut SQLTracker {
    unsafe {
        if GLOBAL_TRACKER.is_none() {
            GLOBAL_TRACKER = Some(SQLTracker::new(None));
        }
        GLOBAL_TRACKER.as_mut().unwrap()
    }
}

/// Example usage:
/// 
/// ```
/// use sqltracker::*;
/// 
/// fn main() {
///     let mut tracker = SQLTracker::new(Some("/tmp/sql_changes.jsonl"));
///     
///     tracker.track_query(
///         "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')",
///         1,
///         Some("mydb"),
///         None,
///         None
///     );
///     
///     let summary = tracker.summary();
///     println!("Total changes: {}", summary.total_changes);
/// }
/// ```

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_operation_to_string() {
        assert_eq!(SQLOperation::Insert.as_str(), "INSERT");
        assert_eq!(SQLOperation::Update.as_str(), "UPDATE");
        assert_eq!(SQLOperation::Delete.as_str(), "DELETE");
        assert_eq!(SQLOperation::Select.as_str(), "SELECT");
    }
}
