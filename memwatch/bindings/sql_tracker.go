package sqltracker

import (
	"C"
	"time"
	"unsafe"
)

// SQL operation types
const (
	OpUnknown = iota
	OpInsert
	OpUpdate
	OpDelete
	OpSelect
)

// SQLChange represents a single column change
type SQLChange struct {
	TimestampNs int64
	TableName   string
	ColumnName  string
	Operation   int
	OldValue    string
	NewValue    string
	RowsAffected int
	Database    string
	FullQuery   string
}

// SQLTracker tracks SQL column-level changes
type SQLTracker struct {
	tracker      unsafe.Pointer
	storagePath  string
	changes      []SQLChange
}

// New creates a new SQL tracker
func New(storagePath string) *SQLTracker {
	// This would load and call the C library
	// C.sql_tracker_init(...)
	
	return &SQLTracker{
		tracker:     nil,
		storagePath: storagePath,
		changes:     make([]SQLChange, 0),
	}
}

// TrackQuery tracks a SQL query and extracts column changes
func (t *SQLTracker) TrackQuery(query string, rowsAffected int, database, oldValue, newValue string) int {
	if t.tracker == nil {
		return 0
	}
	
	// Call native C function
	// return int(C.sql_tracker_track_query(
	//     t.tracker,
	//     C.CString(query),
	//     C.int(rowsAffected),
	//     C.CString(database),
	//     C.CString(oldValue),
	//     C.CString(newValue),
	// ))
	
	return 0
}

// GetChanges returns changes filtered by criteria
func (t *SQLTracker) GetChanges(tableFilter, columnFilter, operationFilter string) []SQLChange {
	var result []SQLChange
	
	for _, change := range t.changes {
		match := true
		
		if tableFilter != "" && change.TableName != tableFilter {
			match = false
		}
		if columnFilter != "" && change.ColumnName != columnFilter {
			match = false
		}
		if operationFilter != "" && operationName(change.Operation) != operationFilter {
			match = false
		}
		
		if match {
			result = append(result, change)
		}
	}
	
	return result
}

// Summary returns summary statistics
type Summary struct {
	TotalChanges int
	Insert       int
	Update       int
	Delete       int
	Select       int
	Tables       map[string]int
	Columns      []string
}

// GetSummary returns statistics about tracked changes
func (t *SQLTracker) GetSummary() *Summary {
	summary := &Summary{
		TotalChanges: len(t.changes),
		Tables:       make(map[string]int),
		Columns:      make([]string, 0),
	}
	
	columnMap := make(map[string]bool)
	
	for _, change := range t.changes {
		switch change.Operation {
		case OpInsert:
			summary.Insert++
		case OpUpdate:
			summary.Update++
		case OpDelete:
			summary.Delete++
		case OpSelect:
			summary.Select++
		}
		
		summary.Tables[change.TableName]++
		
		colKey := change.TableName + "." + change.ColumnName
		if !columnMap[colKey] {
			columnMap[colKey] = true
			summary.Columns = append(summary.Columns, colKey)
		}
	}
	
	return summary
}

// Close frees the tracker
func (t *SQLTracker) Close() {
	if t.tracker != nil {
		// C.sql_tracker_free(t.tracker)
		t.tracker = nil
	}
}

// Global tracker instance
var globalTracker *SQLTracker

// Init initializes the global tracker
func Init(storagePath string) *SQLTracker {
	if globalTracker != nil {
		globalTracker.Close()
	}
	globalTracker = New(storagePath)
	return globalTracker
}

// Get returns the global tracker
func Get() *SQLTracker {
	if globalTracker == nil {
		globalTracker = New("")
	}
	return globalTracker
}

// TrackQuery uses the global tracker
func TrackQuery(query string, rowsAffected int, database, oldValue, newValue string) int {
	return Get().TrackQuery(query, rowsAffected, database, oldValue, newValue)
}

// Helper function to convert operation code to string
func operationName(op int) string {
	switch op {
	case OpInsert:
		return "INSERT"
	case OpUpdate:
		return "UPDATE"
	case OpDelete:
		return "DELETE"
	case OpSelect:
		return "SELECT"
	default:
		return "UNKNOWN"
	}
}

// Example usage (uncomment to test):
/*
func main() {
	tracker := New("/tmp/sql_changes.jsonl")
	defer tracker.Close()
	
	tracker.TrackQuery("INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')", 1, "mydb", "", "")
	tracker.TrackQuery("UPDATE users SET email = 'new@example.com' WHERE id = 1", 1, "mydb", "old@example.com", "new@example.com")
	
	summary := tracker.GetSummary()
	println("Total changes:", summary.TotalChanges)
}
*/
