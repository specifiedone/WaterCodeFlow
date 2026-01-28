using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;

/// <summary>
/// Universal SQL Tracker for C# / .NET
/// Track SQL column-level changes across all databases
/// </summary>
public class SQLTracker : IDisposable
{
    // SQL operation types
    public enum SQLOperation
    {
        Unknown = 0,
        Insert = 1,
        Update = 2,
        Delete = 3,
        Select = 4
    }
    
    /// <summary>
    /// Single column change from SQL operation
    /// </summary>
    public class SQLChange
    {
        public ulong TimestampNs { get; set; }
        public string TableName { get; set; }
        public string ColumnName { get; set; }
        public SQLOperation Operation { get; set; }
        public string OldValue { get; set; }
        public string NewValue { get; set; }
        public int RowsAffected { get; set; }
        public string Database { get; set; }
        public string FullQuery { get; set; }
    }
    
    /// <summary>
    /// Summary statistics
    /// </summary>
    public class Summary
    {
        public int TotalChanges { get; set; }
        public int InsertCount { get; set; }
        public int UpdateCount { get; set; }
        public int DeleteCount { get; set; }
        public int SelectCount { get; set; }
        
        public Dictionary<string, int> Operations
        {
            get
            {
                return new Dictionary<string, int>
                {
                    { "INSERT", InsertCount },
                    { "UPDATE", UpdateCount },
                    { "DELETE", DeleteCount },
                    { "SELECT", SelectCount }
                };
            }
        }
    }
    
    // P/Invoke declarations for native library
    [DllImport("sql_tracker")]
    private static extern IntPtr sql_tracker_init(string storage_path);
    
    [DllImport("sql_tracker")]
    private static extern int sql_tracker_track_query(
        IntPtr tracker,
        string query,
        int rows_affected,
        string database,
        string old_value,
        string new_value
    );
    
    [DllImport("sql_tracker")]
    private static extern void sql_tracker_free(IntPtr tracker);
    
    private IntPtr _tracker;
    private string _storagePath;
    private List<SQLChange> _changes;
    
    /// <summary>
    /// Create new tracker
    /// </summary>
    public SQLTracker(string storagePath = null)
    {
        _storagePath = storagePath;
        _tracker = sql_tracker_init(storagePath);
        _changes = new List<SQLChange>();
    }
    
    /// <summary>
    /// Track a SQL query and extract column changes
    /// </summary>
    public int TrackQuery(string query, int rowsAffected = 0,
                         string database = null, string oldValue = null,
                         string newValue = null)
    {
        if (_tracker == IntPtr.Zero) return 0;
        
        return sql_tracker_track_query(
            _tracker,
            query,
            rowsAffected,
            database,
            oldValue,
            newValue
        );
    }
    
    /// <summary>
    /// Get changes with optional filters
    /// </summary>
    public List<SQLChange> GetChanges(string tableFilter = null,
                                      string columnFilter = null,
                                      string operationFilter = null)
    {
        return _changes.Where(change =>
        {
            if (tableFilter != null && change.TableName != tableFilter)
                return false;
            if (columnFilter != null && change.ColumnName != columnFilter)
                return false;
            if (operationFilter != null && change.Operation.ToString().ToUpper() != operationFilter)
                return false;
            return true;
        }).ToList();
    }
    
    /// <summary>
    /// Get all changes
    /// </summary>
    public IReadOnlyList<SQLChange> AllChanges => _changes.AsReadOnly();
    
    /// <summary>
    /// Get summary statistics
    /// </summary>
    public Summary GetSummary()
    {
        var summary = new Summary
        {
            TotalChanges = _changes.Count
        };
        
        foreach (var change in _changes)
        {
            switch (change.Operation)
            {
                case SQLOperation.Insert:
                    summary.InsertCount++;
                    break;
                case SQLOperation.Update:
                    summary.UpdateCount++;
                    break;
                case SQLOperation.Delete:
                    summary.DeleteCount++;
                    break;
                case SQLOperation.Select:
                    summary.SelectCount++;
                    break;
            }
        }
        
        return summary;
    }
    
    /// <summary>
    /// Cleanup resources
    /// </summary>
    public void Dispose()
    {
        if (_tracker != IntPtr.Zero)
        {
            sql_tracker_free(_tracker);
            _tracker = IntPtr.Zero;
        }
    }
    
    /// <summary>
    /// Get operation as string
    /// </summary>
    public static string OperationToString(SQLOperation op)
    {
        return op switch
        {
            SQLOperation.Insert => "INSERT",
            SQLOperation.Update => "UPDATE",
            SQLOperation.Delete => "DELETE",
            SQLOperation.Select => "SELECT",
            _ => "UNKNOWN"
        };
    }
    
    // Global instance
    private static SQLTracker _globalTracker;
    
    /// <summary>
    /// Initialize global tracker
    /// </summary>
    public static SQLTracker Init(string storagePath = null)
    {
        _globalTracker?.Dispose();
        _globalTracker = new SQLTracker(storagePath);
        return _globalTracker;
    }
    
    /// <summary>
    /// Get global tracker instance
    /// </summary>
    public static SQLTracker Instance
    {
        get
        {
            if (_globalTracker == null)
            {
                _globalTracker = new SQLTracker();
            }
            return _globalTracker;
        }
    }
    
    /// <summary>
    /// Example usage
    /// </summary>
    public static void Main(string[] args)
    {
        using (var tracker = new SQLTracker("/tmp/sql_changes.jsonl"))
        {
            // Track queries
            tracker.TrackQuery(
                "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')",
                1,
                "mydb"
            );
            
            tracker.TrackQuery(
                "UPDATE users SET email = 'new@example.com' WHERE id = 1",
                1,
                "mydb",
                "old@example.com",
                "new@example.com"
            );
            
            // Get summary
            var summary = tracker.GetSummary();
            Console.WriteLine($"Total changes: {summary.TotalChanges}");
            Console.WriteLine($"Operations: {string.Join(", ", summary.Operations.Select(kvp => $"{kvp.Key}={kvp.Value}"))}");
        }
    }
}
