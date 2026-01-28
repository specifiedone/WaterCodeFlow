# MemWatch Enhancements - Auto-Detection Features

## What's New

MemWatch now includes **automatic detection** of all variables and SQL changes without requiring code changes.

## New CLI Flags

### `--track-all-vars` - Auto-Track Everything
Automatically tracks ALL variable modifications in your program.

**Before (Manual):**
```c
memwatch_watch(var1, sizeof(var1), "var1", NULL);
memwatch_watch(var2, sizeof(var2), "var2", NULL);
memwatch_watch(var3, sizeof(var3), "var3", NULL);
// ... manually track each variable
```

**After (Automatic):**
```bash
./build/memwatch_cli run ./program --storage data.db --track-all-vars
# ALL variables are tracked automatically!
```

### `--track-sql` - Auto-Detect SQL Changes
Automatically intercepts and logs all database operations without code changes.

**Before (Manual):**
```python
db.execute("UPDATE users SET email = 'new@ex.com'")
track_sql_query("UPDATE users SET email = 'new@ex.com'", rows_affected=1)  # ← Manual call
```

**After (Automatic):**
```bash
./build/memwatch_cli run python3 app.py --storage data.db --track-sql
# SQL operations are captured automatically!
```

### `--scope global|local|both` - Track Where Changes Happen
Control whether to track global variables, local variables, or both.

```bash
--scope global      # Only global variables
--scope local       # Only local variables  
--scope both        # Everything (default)
```

### `--threads` - Thread-Aware Tracking
Track changes at the thread level to see which thread modified what.

```bash
--track-all-vars --threads  # Know which thread changed each variable
```

## Complete Examples

### Example 1: Python with Auto-Detection
```bash
./build/memwatch_cli run python3 app.py \
    --storage tracking.db \
    --track-all-vars \
    --track-sql \
    --scope global \
    --threads
```

This tracks:
- ✅ All variables (no code changes needed)
- ✅ All SQL queries (database intercepted)
- ✅ Only global scope
- ✅ Thread-level granularity

### Example 2: Java with Full Monitoring
```bash
./build/memwatch_cli run java -jar MyApp.jar \
    --storage memory.db \
    --track-all-vars \
    --track-sql \
    --user-func alert.py \
    --user-func-lang python
```

This tracks:
- ✅ Every variable change
- ✅ Every database operation
- ✅ Triggers Python callback on each change (for alerts/logging)

### Example 3: C/C++ Program
```bash
./build/memwatch_cli run ./myprogram \
    --storage output.db \
    --track-all-vars \
    --threads
```

This tracks:
- ✅ All memory modifications
- ✅ Which thread made each change
- ✅ **ZERO code changes to your program**

### Example 4: Go Application
```bash
./build/memwatch_cli run ./go_app \
    --storage go_tracking.db \
    --track-all-vars \
    --track-sql
```

Works with any language - no modifications needed!

## Output & Viewing Results

### View All Changes (Human Readable)
```bash
./build/memwatch_cli read tracking.db
```

### View as JSON
```bash
./build/memwatch_cli read tracking.db --format json
```

### View Only SQL Changes
```bash
./build/memwatch_cli read tracking.db --sql-only
```

### View Only Memory Changes
```bash
./build/memwatch_cli read tracking.db --memory-only
```

### Filter by Variable Name
```bash
./build/memwatch_cli read tracking.db --filter user_data --limit 50
```

## Architecture

### Auto-Detection Flow

```
Your Program (ANY language)
         ↓
  [Automatic Tracking]
    ├─ --track-all-vars    → Hooks memory operations
    ├─ --track-sql         → Intercepts database calls
    └─ --threads           → Records thread IDs
         ↓
  [SQLite Database]
    ├─ memory_changes table
    ├─ sql_changes table
    └─ Indexed by timestamp
         ↓
  [Query & Analyze]
    ./build/memwatch_cli read db.db
```

## Key Advantages

1. **No Code Changes Required**
   - Track existing programs without modification
   - Works with compiled binaries

2. **Automatic SQL Detection**
   - No manual `track_sql_query()` calls
   - Hooks database operations automatically

3. **Scope Control**
   - Track only what you need (global/local/both)
   - Reduces noise in results

4. **Thread-Aware**
   - Know exactly which thread changed what
   - Perfect for multi-threaded debugging

5. **Universal**
   - Works with Python, Java, Go, Rust, C, C++, C#, JavaScript
   - Same CLI flags for all languages

## Comparison: Old vs New

| Aspect | Old Way | New Way |
|--------|---------|---------|
| Code changes | ❌ Required | ✅ None |
| SQL tracking | ❌ Manual calls | ✅ Automatic |
| Variable selection | ❌ Manual | ✅ Auto-all |
| Thread tracking | ❌ Manual | ✅ Built-in |
| Scope control | ❌ Limited | ✅ Flexible |
| Languages | ⚠️ Language-specific | ✅ Universal CLI |
| Setup time | ⏱️ Minutes | ⚡ Seconds |

## Technical Details

### Auto-Variable Tracking
- Scans process memory regions
- Tracks all read/write operations
- Records page-level changes
- Zero instrumentation needed

### Auto-SQL Detection  
- Hooks database library calls (SQLite, MySQL, PostgreSQL)
- Parses SQL statements to extract operations
- Records table, column, and row information
- Works with any database driver

### Thread Awareness
- Captures thread ID with each change
- Allows filtering by thread
- Perfect for debugging race conditions

## Limitations & Notes

- Linux-only (uses mprotect for memory tracking)
- SQL detection requires database library to be linked
- High-frequency changes may impact performance
- Use `--scope local` for local-only tracking to reduce overhead

## Future Enhancements

Planned additions:
- [ ] Real-time streaming to websocket
- [ ] Built-in alerting on pattern detection
- [ ] Distributed tracing support
- [ ] Custom filtering expressions
- [ ] Machine learning anomaly detection

## Getting Started

```bash
# Build with enhancements
make build

# Track everything with auto-detection
./build/memwatch_cli run <your-program> \
    --storage results.db \
    --track-all-vars \
    --track-sql \
    --threads

# View results
./build/memwatch_cli read results.db --format json
```

That's it! No code changes needed. 🚀
