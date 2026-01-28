/**
 * SQL Tracker Example - Java
 * 
 * Demonstrates universal SQL variable tracking in Java.
 * Works with any database (SQLite, PostgreSQL, MySQL, etc.)
 */

public class SQLTrackerExample {
    public static void main(String[] args) {
        System.out.println("=== SQL Tracker Example - Java ===\n");
        
        // Initialize tracker (global instance)
        SQLTracker tracker = SQLTracker.init("/tmp/sql_changes.jsonl");
        
        // Example 1: Track INSERT
        System.out.println("Tracking INSERT query...");
        int inserted = tracker.trackQuery(
            "INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30)",
            1,
            "mydb",
            null,
            null
        );
        System.out.println("Tracked " + inserted + " column changes\n");
        
        // Example 2: Track UPDATE
        System.out.println("Tracking UPDATE query...");
        int updated = tracker.trackQuery(
            "UPDATE users SET email = 'newemail@example.com', age = 31 WHERE id = 1",
            1,
            "mydb",
            "alice@example.com",
            "newemail@example.com"
        );
        System.out.println("Tracked " + updated + " column changes\n");
        
        // Example 3: Track DELETE
        System.out.println("Tracking DELETE query...");
        int deleted = tracker.trackQuery(
            "DELETE FROM users WHERE id = 1",
            1,
            "mydb",
            null,
            null
        );
        System.out.println("Tracked " + deleted + " column changes\n");
        
        // Example 4: Track SELECT
        System.out.println("Tracking SELECT query...");
        int selected = tracker.trackQuery(
            "SELECT name, email FROM users WHERE age > 25",
            5,
            "mydb",
            null,
            null
        );
        System.out.println("Tracked " + selected + " column changes\n");
        
        // Example 5: Monitor sensitive columns
        System.out.println("=== Monitoring Sensitive Columns ===");
        monitorSensitiveOperations(tracker);
        System.out.println();
        
        // Example 6: Get changes by filter
        System.out.println("=== Changes to 'users' table ===");
        var userChanges = tracker.getChanges("users", null, null);
        for (SQLTracker.SQLChange change : userChanges) {
            System.out.println("- " + change.table_name + "." + change.column_name +
                             " [" + change.operation.name() + "] (rows: " + change.rows_affected + ")");
        }
        System.out.println();
        
        // Example 7: Get summary
        System.out.println("=== Summary Statistics ===");
        SQLTracker.Summary summary = tracker.getSummary();
        System.out.println("Total changes: " + summary.total_changes);
        System.out.println("Operations: " + summary.getOperationCounts());
        System.out.println();
        
        tracker.close();
    }
    
    /**
     * Monitor sensitive column modifications
     */
    private static void monitorSensitiveOperations(SQLTracker tracker) {
        String[] sensitiveColumns = {"password", "credit_card", "ssn", "api_key", "secret"};
        
        // Simulate monitoring queries
        String[] queries = {
            "UPDATE users SET password = 'hashed_password' WHERE id = 1",
            "INSERT INTO payment (user_id, credit_card) VALUES (1, '4111-1111-1111-1111')",
            "SELECT ssn FROM employees WHERE department = 'HR'"
        };
        
        for (String query : queries) {
            tracker.trackQuery(query, 1, "mydb", null, null);
            
            // Check for sensitive operations
            for (String sensitive : sensitiveColumns) {
                if (query.toLowerCase().contains(sensitive)) {
                    System.out.println("🚨 ALERT: Sensitive field '" + sensitive + "' accessed!");
                    System.out.println("   Query: " + query);
                }
            }
        }
    }
}

/*
 * Compile with:
 *   javac -cp bindings SQLTrackerExample.java bindings/SQLTracker.java
 * 
 * Run with:
 *   java -cp bindings:. SQLTrackerExample
 * 
 * Note: Requires JNA library for native library bindings
 *   https://github.com/java-native-access/jna
 */
