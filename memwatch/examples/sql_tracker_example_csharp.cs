/**
 * SQL Tracker Example - C#
 * 
 * Demonstrates universal SQL variable tracking in C#.
 * Works with any database (SQLite, PostgreSQL, MySQL, etc.)
 */

using System;
using System.Collections.Generic;
using System.Linq;

class SQLTrackerExample
{
    static void Main(string[] args)
    {
        Console.WriteLine("=== SQL Tracker Example - C# ===\n");
        
        // Initialize tracker
        using (var tracker = new SQLTracker("/tmp/sql_changes.jsonl"))
        {
            // Example 1: Track INSERT
            Console.WriteLine("Tracking INSERT query...");
            int inserted = tracker.TrackQuery(
                "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
                1,
                "mydb"
            );
            Console.WriteLine($"Tracked {inserted} column changes\n");
            
            // Example 2: Track UPDATE
            Console.WriteLine("Tracking UPDATE query...");
            int updated = tracker.TrackQuery(
                "UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
                1,
                "mydb",
                "alice@example.com",
                "newemail@example.com"
            );
            Console.WriteLine($"Tracked {updated} column changes\n");
            
            // Example 3: Track DELETE
            Console.WriteLine("Tracking DELETE query...");
            int deleted = tracker.TrackQuery(
                "DELETE FROM users WHERE id = 1",
                1,
                "mydb"
            );
            Console.WriteLine($"Tracked {deleted} column changes\n");
            
            // Example 4: Track SELECT
            Console.WriteLine("Tracking SELECT query...");
            int selected = tracker.TrackQuery(
                "SELECT name, email FROM users WHERE age > 25",
                5,
                "mydb"
            );
            Console.WriteLine($"Tracked {selected} column changes\n");
            
            // Example 5: Monitor sensitive fields
            Console.WriteLine("=== Monitoring Sensitive Fields ===");
            MonitorSensitiveOperations(tracker);
            Console.WriteLine();
            
            // Example 6: Get changes by filter
            Console.WriteLine("=== Changes to 'users' table ===");
            var userChanges = tracker.GetChanges("users");
            foreach (var change in userChanges)
            {
                Console.WriteLine($"- {change.TableName}.{change.ColumnName} " +
                                $"[{change.Operation.ToString().ToUpper()}] " +
                                $"(rows: {change.RowsAffected})");
            }
            Console.WriteLine();
            
            // Example 7: Get summary
            Console.WriteLine("=== Summary Statistics ===");
            var summary = tracker.GetSummary();
            Console.WriteLine($"Total changes: {summary.TotalChanges}");
            foreach (var op in summary.Operations)
            {
                Console.WriteLine($"  {op.Key}: {op.Value}");
            }
            Console.WriteLine();
            
            // Example 8: Persistence example
            Console.WriteLine("=== Persistent Storage ===");
            Console.WriteLine($"Changes saved to: /tmp/sql_changes.jsonl");
            Console.WriteLine("Query file with: grep '\"operation\": \"UPDATE\"' /tmp/sql_changes.jsonl");
        }
    }
    
    /**
     * Monitor sensitive field modifications
     */
    static void MonitorSensitiveOperations(SQLTracker tracker)
    {
        string[] sensitiveFields = { "password", "credit_card", "ssn", "api_key", "secret" };
        
        string[] queries = new[]
        {
            "UPDATE users SET password = 'hashed_pw' WHERE id = 1",
            "INSERT INTO payment (user_id, credit_card) VALUES (1, '4111-1111-1111-1111')",
            "SELECT ssn FROM employees WHERE department = 'HR'",
            "UPDATE accounts SET api_key = 'sk_live_abc123' WHERE user_id = 5"
        };
        
        foreach (string query in queries)
        {
            tracker.TrackQuery(query, 1, "mydb");
            
            // Check for sensitive operations
            string queryLower = query.ToLower();
            foreach (string field in sensitiveFields)
            {
                if (queryLower.Contains(field))
                {
                    Console.WriteLine($"🚨 ALERT: Sensitive field '{field}' accessed!");
                    Console.WriteLine($"   Query: {query}");
                }
            }
        }
    }
}

/*
 * Compile with:
 *   csc sql_tracker.cs examples/sql_tracker_example_csharp.cs
 * 
 * Run with:
 *   ./sql_tracker_example_csharp
 * 
 * Note: Requires .NET runtime and SQLTracker class compiled together
 *       SQLTracker uses P/Invoke for native library integration
 */
