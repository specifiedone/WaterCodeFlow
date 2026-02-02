# MemWatch — Concise Implementation Spec

> Purpose: minimal, unambiguous, build-ready spec for the hackathon MVP. No fluff.

---

## Quick summary

* Scope: **single-process, same-user**, Linux-only MVP.
* Guarantee: every write that reaches a registered page generates **at least one event** (no silent misses).
* Mechanism: Linux `userfaultfd` WRITE-PROTECT (WP) mode.
* Strategy: one tracked variable → one page (mmap). Adapter maps high-level variable → page address and registers it with core.
* Fault flow: write → kernel blocks thread → core reads uffd_msg → read `/proc/<tid>/syscall` for IP → resolve symbol → UFFDIO_WRITEPROTECT(mode=0) → write completes → UFFDIO_WRITEPROTECT(mode=WP).

---

## Requirements

* OS: Linux with `userfaultfd` and WP support (kernel ≳ 4.14; WP improvements later). Document kernel min version in README.
* Build: gcc/clang, pthreads.
* Binaries for demo compiled with `-g` (debug symbols).
* Privileges: same-user process; no special capabilities for fast-path procfs method.

---

## Core primitives (APIs & ioctls)

> All ioctls are issued by **core only**. Adapters never touch `userfaultfd`.

* `syscall(__NR_userfaultfd, flags)` → fd
* `UFFDIO_API` with `uffdio_api.api = UFFD_API` and `uffdio_api.features |= UFFD_FEATURE_THREAD_ID`
* `UFFDIO_REGISTER` with `.mode = UFFDIO_REGISTER_MODE_WP`
* `UFFDIO_WRITEPROTECT` to set/clear WP on a page range
* Read `struct uffd_msg` from userfaultfd for events (look for `msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP`)

Key structs: `uffdio_api`, `uffdio_register`, `uffdio_writeprotect`, `uffd_msg` (kernel headers).

---

## Data model (core)

* `PageEntry { page_base, page_len (4k), variable_id, adapter_id, pre_snapshot_ptr }`
* `DirtyEvent { event_id, ts_ns, page_base, fault_addr, tid, ip, symbol, pre_checksum }`
* Persistent event after slow-path: include delta ranges and variable_id mapping.

Identifier rules: `variable_id = uuid()` assigned by adapter on register.

---

## Adapter → Core contract (concrete)

> This is a **hard boundary**. If something cannot be expressed here, it does not belong in core. **register_variable(address_or_value, size, flags, human_name) -> variable_id**

* If `address_or_value` is a runtime value (stack/local), adapter must allocate a page (`mmap`) and copy value (shadow-copy) and return page address.
* Adapter must `touch` page (write one byte) before registering.
* Adapter must guarantee lifetime until `unregister_variable(variable_id)` called.

**unregister_variable(variable_id)**
**read_snapshot(variable_id) -> bytes**
**write_snapshot(variable_id, bytes)**

Flags: `track_threads`, `track_sql`, `output_dir`, `mutation_depth`.

---

## Fast-path handler pseudocode (must be implemented in C)

> Ordering matters. Do **not** reorder unprotect / re-protect steps.

```c
// setup userfaultfd, enable features
int uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
ioctl(uffd, UFFDIO_API, &api);

// register pages (per adapter calls)
ioctl(uffd, UFFDIO_REGISTER, &reg);
ioctl(uffd, UFFDIO_WRITEPROTECT, &wp_set);

// handler thread loop
while (read(uffd, &msg, sizeof(msg)) == sizeof(msg)) {
  if (!(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP)) continue;
  page_base = msg.arg.pagefault.address & ~4095UL;
  tid = msg.arg.pagefault.feat.ptid; // requires UFFD_FEATURE_THREAD_ID
  // fast IP extraction
  ip = get_ip_from_proc_syscall(tid); // parse /proc/<tid>/syscall
  emit_minimal_event_enqueue(page_base, msg.arg.pagefault.address, tid, ip);
  // unprotect -> kernel allows write (write completes before resuming)
  wp_clear.range.start = page_base; wp_clear.range.len = 4096; wp_clear.mode = 0;
  ioctl(uffd, UFFDIO_WRITEPROTECT, &wp_clear);
  // re-protect
  wp_clear.mode = UFFDIO_WRITEPROTECT_MODE_WP; ioctl(uffd, UFFDIO_WRITEPROTECT, &wp_clear);
}
```

Notes: critical path must avoid heavy allocations; enqueue minimal record for background processing.

---

## Slow-path processing (background worker)

> Runs in a separate thread or process. Fast-path must remain O(1) per fault.

* Take event, read `pre_snapshot` (from registry), read `post_snapshot` from page_base memory, compute delta (memcmp), compute checksums, map delta offsets to adapter-provided variable ranges, enrich event, persist JSONL to `output_dir`.
* Symbol resolution: call `addr2line -e /proc/<pid>/exe -f -C 0x<ip>` once per unique `ip` and cache results. Replace with `libdw` for performance if time.

---

## Instruction pointer extraction (fast, no ptrace)

* Read `/proc/<tid>/syscall` line: parse last hex token → `ip`.
* Fallback if unreadable: use `PTRACE_SEIZE + PTRACE_INTERRUPT + PTRACE_GETREGS` (requires capability/ptrace_scope changes). Document fallback.

---

## Event schema (JSONL minimal fields)

```json
{ "event_id":"u","ts_ns":0, "page_base":"0x...", "fault_addr":"0x...", "tid":1234, "ip":"0x...", "symbol":"func","file":"/path.c","line":42, "deltas":[{"offset":0,"len":4,"old":"...","new":"..."}], "variable_ids":["v-uuid"] }
```

---

## Stack handling (adapter algorithm)

1. Adapter detects stack/local value to watch.
2. `p = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);`
3. memcpy(p, &orig_value, size); `touch(p);`
4. create proxy that reads/writes `p` (language binding) and register `p` with core.
5. Ensure all future writes use proxy (adapter must rewrite references / provide proxy API).

---

## Failure modes (explicit)

> All of these must be logged, none are fatal for MVP.

* `addr2line` returns `??` if no debug symbols → show IP only.
* Hot tight loops produce many faults → provide sampling or disable for that variable.
* Dense pages with multiple logical variables → ambiguous deltas; avoid in demo.
* `/proc/<tid>/syscall` unreadable in container / seccomp → fallback to ptrace.

---

## Tests (must pass)

* E2E: watch variable, write, assert event file:line equals writing line.
* Multi-thread: two threads write same page; events show both tids and correct ordering.
* Stack relocation: watch stack var, mutate, event contains correct delta.
* Failure tests: missing `-g`, heavy write storms.

---

## Minimal build & run (commands)

```bash
gcc -g -O0 -o memwatch core/uffd_core.c -pthread
# run your demo program that links to adapter which registers page
./memwatch
```

---

## 19-day micro plan (tasks)

* Day 1: core uffd loop + single page register + handler enqueue
* Day 2: fast IP extraction (/proc/syscall) + addr2line cache
* Day 3: slow-path diff + JSONL events
* Day 4: python adapter + watch(x) sugar (proxy & register)
* Day 5: multi-thread tests + stack relocation tests
* Day 6: CLI + simple web UI
* Day 7: polish + README + demo script

---

## Non-goals (do not attempt for hackathon)

* Transparent `watch(x)` without adapter relocation
* Tracking all program variables
* Cross-process transparent watching

---

If you want, I will now replace the open MD canvas with this exact content (done), and then generate `core/uffd_core.c` minimal skeleton implementing the handler loop above. Which do you want next?

---

## SQL Interception & Tracking Features (ADDENDUM)

> Minimal, concrete guidance so adapters can reliably observe SQL client activity (queries, parameters, result buffer mutations) and map those to MemWatch events.

### Goals

* Observe when SQL operations mutate program-visible buffers (client-side params, result buffers, cursor state) and emit MemWatch events tied to those mutations.
* Capture query text, parameter values, affected row identifiers (when available), and transaction boundaries.

### Adapter responsibilities for SQL interception (concrete)

1. **Library hooks** — the adapter should instrument the most-used SQL client APIs for the language and wrap the following functions/methods: `connect()`, `cursor()/prepare()`, `execute()/executemany()`, `fetch*()` and `commit()/rollback()`.
2. **On cursor/statement creation**: allocate or identify backing memory for:

   * query text (if the client builds it client-side)
   * parameter blob(s) (serialized form)
   * result buffer placeholder (for result rows fetched into memory)
     Register these pages with core using `register_variable(...)` and keep mapping: `cursor_id -> [page_ids]`.
3. **On execute() call**:

   * snapshot pre-state of parameter pages and result buffers (if any)
   * emit an event metadata entry `{type: "sql.execute", query: <text>, params_ptrs: [...], cursor_id, ts}` into fast-path enqueue (minimal)
   * allow the call to run (the adapter may use the core unprotect / re-protect flow only if the query-building code writes into its registered pages)
4. **On fetch()/cursor advance**:

   * when the client library writes result rows into a user-provided buffer, that buffer must be previously registered; the core will catch page faults and adapter will map delta ranges to row/column positions.
   * emit `sql.result_row_written` events enriched with row/column offsets when deltas are observed in slow-path
5. **Transaction boundaries**:

   * on `commit()`/`rollback()`, emit an enriched event that groups prior SQL events for this transaction id; useful to correlate mutations with transactional semantics.
6. **Edge cases**:

   * if client uses streaming cursors (server-side), adapter should still register any client-side buffers or use query/param-level events instead of row-level deltas.

### Popular client libraries (implement these wrappers for MVP)

* **Python**: `psycopg2`, `asyncpg`, `pymysql`, `sqlite3` – wrap `cursor`, `execute`, `executemany`, `fetchone`, `fetchall`.
* **Node.js**: `node-postgres (pg)`, `mysql2` – instrument `client.query`, `pool.query`, and streams.
* **Java (JDBC)**: wrap `Connection.prepareStatement`, `PreparedStatement.execute`, `ResultSet.next()`.
* **Go**: `database/sql` – wrap `DB.Query`, `Stmt.Exec`, `Rows.Scan` (with cgo-backed buffers when possible).
* **C#/.NET**: `System.Data.*` – wrap `DbCommand.Execute*`, `DbDataReader.Read()`.
* **Ruby**: `pg`, `mysql2`, ActiveRecord adapters – wrap `execute` and `fetch` paths.
* **PHP**: `PDO` – wrap `PDO::prepare`, `PDOStatement::execute`, `fetch`.
* **Rust**: `diesel`, `sqlx` – provide helpers to allocate and register param/result buffers (macro-based wrappers may be easiest).
* **C/C++**: `libpq`, `mysqlclient` – instrument at application layer or provide wrapper functions.
* **Kotlin**: same as Java (JDBC) or Android SQLite APIs.

### What to track per SQL event

* `query_text` (string)
* `params_serialized_ptrs` (list of page addresses) and a small inline preview (first N bytes) for quick triage
* `cursor_id` / `statement_id`
* `affected_rows` when available (from DB client return)
* `transaction_id` (adapter-managed) for grouping
* `row_key` or primary key candidate if adapter can parse result set schema
* `pre_snapshot_checksum` and `post_snapshot_checksum` for parameter/result buffers

### How to map SQL buffer deltas to high-level semantics

* The slow-path diff will produce byte-range deltas on pages registered for the cursor/result buffer.
* Adapter must interpret offset→row/column mapping using the client library's buffer layout (adapter knows this for the specific client) and annotate event deltas accordingly.

### Correctness & safety checks for SQL adapters

* **Prefault**: ensure all registered buffers are touched before registration.
* **Atomicity**: for `executemany` or large batch operations, adapter should emit start/end markers and group internal events to avoid misattribution.
* **Schema-awareness**: when possible, adapter should parse result set metadata to map byte offsets to fields (for correct delta->column attribution).
* **Parameter copying**: do not assume client stores params in a single contiguous buffer—if needed, create a canonical serialized params page and register that instead.

---

## Feature flags & runtime options (explicit)

Add these to the adapter `register_variable` flags and expose them via CLI / config:

* `track_threads` (bool): include thread id with each event (core supports this via UFFD_FEATURE_THREAD_ID). Default: true.
* `track_all` (bool): register all candidate variables provided by adapter; use with caution (memory heavy). Default: false.
* `files_scope` (list<string>): only register variables originating from listed source files (glob support). Default: all files.
* `track_locals` (bool): allow adapter to relocate stack/local variables into backing pages and register them. Default: false (explicit opt-in).
* `mutation_depth` (int bytes): number of bytes to snapshot/diff for each event; `0` implies full-page snapshot. Default: full-page.
* `sql_tracking` (enum): `none | basic | detailed`:

  * `none`: do not instrument SQL libraries
  * `basic`: emit `sql.execute` / `sql.commit` events with query and param previews
  * `detailed`: register result/param buffers and emit row-level deltas
* `custom_processor` (path to executable/script): if set, core will POST the enriched slow-path event to this process's stdin (JSON) and accept its optional decisions (e.g., drop, annotate, enrich). The processor must return a small JSON action object.

### CLI example

```
memwatch --output ./events --track-threads=true --track-locals=true --files-scope="src/**" --mutation-depth=64 --sql-tracking=detailed --custom-processor=./processor.py
```

### Custom processor contract (minimal)

* Core writes one JSON event per line to the processor's stdin.
* Processor reads event, may add fields or return action JSON on stdout (synchronous, timeout 100ms default).
* Action JSON schema (example): `{ "action": "annotate", "annotations": {"risk": "high"} }` or `{ "action": "drop" }` or `{ "action": "enrich", "extra": {...} }`.
* Core will merge annotations and obey `drop` for persistence decisions.
