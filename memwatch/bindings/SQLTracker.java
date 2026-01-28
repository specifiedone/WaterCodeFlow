import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import java.util.*;

/**
 * Universal SQL Tracker for Java
 * Track SQL column-level changes across all databases
 */
public class SQLTracker {
    /**
     * JNA Interface to native sql_tracker library
     */
    public interface SqlTrackerLib extends Library {
        SqlTrackerLib INSTANCE = Native.load("sql_tracker", SqlTrackerLib.class);
        
        Pointer sql_tracker_init(String storage_path);
        int sql_tracker_track_query(Pointer tracker, String query, int rows_affected,
                                   String database, String old_value, String new_value);
        void sql_tracker_free(Pointer tracker);
        void sql_tracker_summary(Pointer tracker, SqlTrackerSummary summary);
    }
    
    /**
     * SQL operation types
     */
    public enum SQLOperation {
        UNKNOWN(0),
        INSERT(1),
        UPDATE(2),
        DELETE(3),
        SELECT(4);
        
        public final int value;
        
        SQLOperation(int value) {
            this.value = value;
        }
        
        public static SQLOperation from(int value) {
            for (SQLOperation op : values()) {
                if (op.value == value) return op;
            }
            return UNKNOWN;
        }
    }
    
    /**
     * Single column change
     */
    public static class SQLChange {
        public long timestamp_ns;
        public String table_name;
        public String column_name;
        public SQLOperation operation;
        public String old_value;
        public String new_value;
        public int rows_affected;
        public String database;
        public String full_query;
        
        public SQLChange() {}
        
        public SQLChange(long timestamp_ns, String table_name, String column_name,
                        SQLOperation operation, String old_value, String new_value,
                        int rows_affected, String database, String full_query) {
            this.timestamp_ns = timestamp_ns;
            this.table_name = table_name;
            this.column_name = column_name;
            this.operation = operation;
            this.old_value = old_value;
            this.new_value = new_value;
            this.rows_affected = rows_affected;
            this.database = database;
            this.full_query = full_query;
        }
    }
    
    /**
     * Summary statistics
     */
    public static class Summary extends Structure {
        public int total_changes;
        public int insert_count;
        public int update_count;
        public int delete_count;
        public int select_count;
        
        public Map<String, Integer> getOperationCounts() {
            Map<String, Integer> counts = new HashMap<>();
            counts.put("INSERT", insert_count);
            counts.put("UPDATE", update_count);
            counts.put("DELETE", delete_count);
            counts.put("SELECT", select_count);
            return counts;
        }
    }
    
    // JNA structure for summary (must match C struct)
    public static class SqlTrackerSummary extends Structure {
        public int total_changes;
        public int insert_count;
        public int update_count;
        public int delete_count;
        public int select_count;
    }
    
    private Pointer tracker;
    private String storage_path;
    private List<SQLChange> changes;
    
    /**
     * Initialize tracker
     */
    public SQLTracker() {
        this(null);
    }
    
    /**
     * Initialize tracker with optional storage path
     */
    public SQLTracker(String storage_path) {
        this.storage_path = storage_path;
        this.tracker = SqlTrackerLib.INSTANCE.sql_tracker_init(storage_path);
        this.changes = new ArrayList<>();
    }
    
    /**
     * Track a SQL query
     */
    public int trackQuery(String query, int rows_affected,
                         String database, String old_value, String new_value) {
        if (tracker == null) return 0;
        
        return SqlTrackerLib.INSTANCE.sql_tracker_track_query(
            tracker, query, rows_affected, database, old_value, new_value
        );
    }
    
    /**
     * Track a SQL query (minimal args)
     */
    public int trackQuery(String query) {
        return trackQuery(query, 0, null, null, null);
    }
    
    /**
     * Get changes by filter
     */
    public List<SQLChange> getChanges(String table_filter, String column_filter,
                                      String operation_filter) {
        List<SQLChange> result = new ArrayList<>();
        
        for (SQLChange change : changes) {
            boolean match = true;
            
            if (table_filter != null && !change.table_name.equals(table_filter)) {
                match = false;
            }
            if (column_filter != null && !change.column_name.equals(column_filter)) {
                match = false;
            }
            if (operation_filter != null && !change.operation.name().equals(operation_filter)) {
                match = false;
            }
            
            if (match) {
                result.add(change);
            }
        }
        
        return result;
    }
    
    /**
     * Get all changes
     */
    public List<SQLChange> getChanges() {
        return new ArrayList<>(changes);
    }
    
    /**
     * Get summary statistics
     */
    public Summary getSummary() {
        SqlTrackerSummary native_summary = new SqlTrackerSummary();
        SqlTrackerLib.INSTANCE.sql_tracker_summary(tracker, native_summary);
        
        Summary summary = new Summary();
        summary.total_changes = native_summary.total_changes;
        summary.insert_count = native_summary.insert_count;
        summary.update_count = native_summary.update_count;
        summary.delete_count = native_summary.delete_count;
        summary.select_count = native_summary.select_count;
        
        return summary;
    }
    
    /**
     * Cleanup
     */
    public void close() {
        if (tracker != null) {
            SqlTrackerLib.INSTANCE.sql_tracker_free(tracker);
            tracker = null;
        }
    }
    
    /**
     * Cleanup on garbage collection
     */
    @Override
    protected void finalize() {
        close();
    }
    
    // Global instance
    private static SQLTracker global_tracker = null;
    
    /**
     * Initialize global tracker
     */
    public static synchronized SQLTracker init(String storage_path) {
        if (global_tracker != null) {
            global_tracker.close();
        }
        global_tracker = new SQLTracker(storage_path);
        return global_tracker;
    }
    
    /**
     * Get global tracker
     */
    public static synchronized SQLTracker getInstance() {
        if (global_tracker == null) {
            global_tracker = new SQLTracker();
        }
        return global_tracker;
    }
    
    /**
     * Example usage
     */
    public static void main(String[] args) {
        SQLTracker tracker = new SQLTracker();
        
        // Track queries
        tracker.trackQuery("INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')", 1, "mydb", null, null);
        tracker.trackQuery("UPDATE users SET email = 'new@example.com' WHERE id = 1", 1, "mydb", "old@example.com", "new@example.com");
        
        // Get summary
        Summary summary = tracker.getSummary();
        System.out.println("Total changes: " + summary.total_changes);
        System.out.println("Operations: " + summary.getOperationCounts());
        
        tracker.close();
    }
}
