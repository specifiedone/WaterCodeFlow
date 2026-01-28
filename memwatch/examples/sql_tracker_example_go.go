package main

import (
	"fmt"
	"./sqltracker"
)

/**
 * SQL Tracker Example - Go
 * 
 * Demonstrates universal SQL variable tracking in Go.
 */

func main() {
	fmt.Println("=== SQL Tracker Example - Go ===\n")
	
	// Initialize tracker
	tracker := sqltracker.New("/tmp/sql_changes.jsonl")
	defer tracker.Close()
	
	// Example 1: Track INSERT
	fmt.Println("Tracking INSERT query...")
	inserted := tracker.TrackQuery(
		"INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
		1,
		"mydb",
		"",
		"",
	)
	fmt.Printf("Tracked %d column changes\n\n", inserted)
	
	// Example 2: Track UPDATE
	fmt.Println("Tracking UPDATE query...")
	updated := tracker.TrackQuery(
		"UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
		1,
		"mydb",
		"alice@example.com",
		"newemail@example.com",
	)
	fmt.Printf("Tracked %d column changes\n\n", updated)
	
	// Example 3: Track DELETE
	fmt.Println("Tracking DELETE query...")
	deleted := tracker.TrackQuery(
		"DELETE FROM users WHERE id = 1",
		1,
		"mydb",
		"",
		"",
	)
	fmt.Printf("Tracked %d column changes\n\n", deleted)
	
	// Example 4: Track SELECT
	fmt.Println("Tracking SELECT query...")
	selected := tracker.TrackQuery(
		"SELECT name, email FROM users WHERE age > 25",
		5,
		"mydb",
		"",
		"",
	)
	fmt.Printf("Tracked %d column changes\n\n", selected)
	
	// Example 5: Monitor sensitive fields
	fmt.Println("=== Monitoring Sensitive Fields ===")
	monitorSensitiveOperations(tracker)
	fmt.Println()
	
	// Example 6: Get changes by filter
	fmt.Println("=== Changes to 'users' table ===")
	changes := tracker.GetChanges("users", "", "")
	for _, change := range changes {
		fmt.Printf("- %s.%s [%s] (rows: %d)\n",
			change.TableName,
			change.ColumnName,
			operationName(change.Operation),
			change.RowsAffected,
		)
	}
	fmt.Println()
	
	// Example 7: Get summary
	fmt.Println("=== Summary Statistics ===")
	summary := tracker.GetSummary()
	fmt.Printf("Total changes: %d\n", summary.TotalChanges)
	fmt.Printf("INSERT: %d, UPDATE: %d, DELETE: %d, SELECT: %d\n",
		summary.Insert,
		summary.Update,
		summary.Delete,
		summary.Select,
	)
	fmt.Printf("Tables: %v\n", summary.Tables)
}

/**
 * Monitor sensitive field modifications
 */
func monitorSensitiveOperations(tracker *sqltracker.SQLTracker) {
	sensitiveFields := []string{
		"password",
		"credit_card",
		"ssn",
		"api_key",
		"secret",
	}
	
	queries := []string{
		"UPDATE users SET password = 'hashed_pw' WHERE id = 1",
		"INSERT INTO payment (user_id, credit_card) VALUES (1, '4111-1111-1111-1111')",
		"SELECT ssn FROM employees WHERE department = 'HR'",
	}
	
	for _, query := range queries {
		tracker.TrackQuery(query, 1, "mydb", "", "")
		
		// Check for sensitive operations
		for _, field := range sensitiveFields {
			// Simple substring check (real implementation would use SQL parser)
			if contains(query, field) {
				fmt.Printf("🚨 ALERT: Sensitive field '%s' accessed!\n", field)
				fmt.Printf("   Query: %s\n", query)
			}
		}
	}
}

func operationName(op int) string {
	switch op {
	case sqltracker.OpInsert:
		return "INSERT"
	case sqltracker.OpUpdate:
		return "UPDATE"
	case sqltracker.OpDelete:
		return "DELETE"
	case sqltracker.OpSelect:
		return "SELECT"
	default:
		return "UNKNOWN"
	}
}

func contains(s, substr string) bool {
	for i := 0; i < len(s); i++ {
		if i+len(substr) <= len(s) {
			if s[i:i+len(substr)] == substr {
				return true
			}
		}
	}
	return false
}

/*
 * Build with:
 *   go build -o sql_tracker_example_go examples/sql_tracker_example_go.go
 * 
 * Run with:
 *   ./sql_tracker_example_go
 */
