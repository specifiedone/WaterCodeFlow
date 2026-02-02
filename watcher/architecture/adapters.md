# MemWatch Adapters Architecture — Python & JavaScript

> Scope: **Adapters only**. Build-ready, no fluff. Focus on correctness, explicit boundaries, and wiring to the existing core (userfaultfd-based). Covers **Python** and **JavaScript (Node.js)** adapters, including **SQL interception**, **thread tracking**, and **CLI-configurable features**.

---

## Adapter Principles (Non‑Negotiable)

1. **Core ignorance**: core never knows what a variable is; it only sees pages.
2. **Adapter ownership**: adapter owns and stabilizes memory (mmap / native buffer).
3. **Forced routing**: all writes to watched values must pass through adapter‑owned memory.
4. **Explicitness**: transparent watching of arbitrary high‑level objects is **not supported**; proxies are required.
5. **Correctness > convenience**: documented limits are acceptable; silent misses are not.

---

## Adapter ↔ Core ABI (Shared)

Adapters implement this ABI and call into the core via FFI / IPC.

### Required calls

* `register_page(page_base: void*, size=4096, flags) -> variable_id`
* `unregister_page(variable_id)`
* `update_metadata(variable_id, meta)`

### Required flags (bitmask)

* `TRACK_THREADS`
* `TRACK_SQL`
* `TRACK_ALL_VARS` (adapter-defined semantics)
* `TRACK_LOCALS`
* `MUTATION_DEPTH` (byte range or FULL)

### Adapter guarantees

* Page is **touched** before registration.
* Page lifetime ≥ watch lifetime.
* All writes to the watched value hit the page.

---

# Python Adapter

## Design Summary

* Language: CPython
* Strategy: **Proxy + shadow memory page**
* Watch semantics: `x = watch(x)` (mandatory reassignment)
* Memory: `mmap` one page per watched variable

---

## Python Adapter Components

### 1. Watch API

```python
x = watch(x, *, track_threads=True, track_sql=False,
          mutation_depth="FULL", name="x")
```

Returns a **Proxy** object. No in-place magic.

---

### 2. Shadow Memory Manager

* `mmap(4096, PROT_READ|PROT_WRITE)`
* Serialize initial value → page
* Keep:

  * `variable_id`
  * `page_base`
  * serializer / deserializer

**Supported serializers (MVP)**

* int / float (fixed width)
* bytes / bytearray
* small structs via `struct`

Objects with dynamic layout must be proxied field-by-field.

---

### 3. Proxy Object (Critical)

Proxy **must intercept all mutations**.

Required dunder hooks:

* `__setattr__`
* `__setitem__`
* arithmetic ops (`__iadd__`, etc.)
* buffer protocol (if applicable)

**Rule**: no mutation bypasses page writes.

Reads:

* deserialize from page

Writes:

* write bytes into page
* never replace page

---

### 4. Stack & Local Variables (Python)

* Locals cannot be safely watched directly.
* Adapter must **relocate**:

  1. Allocate page
  2. Copy initial value
  3. Return proxy
  4. User must use proxy thereafter

This is enforced and documented.

---

### 5. Thread Tracking (Python)

* Adapter does **not** manage threads
* Core receives `tid` from userfaultfd
* Adapter annotates events with:

  * `threading.get_ident()` at watch time
  * optional logical thread name

---

## Python SQL Tracking

### Goal

Detect **which variables are mutated due to SQL operations** and annotate events.

### Interception Strategy

Monkey‑patch popular DB APIs **at adapter init**.

#### Libraries covered (MVP)

* `sqlite3`
* `psycopg2`
* `psycopg`
* `mysqlclient`
* `sqlalchemy` (engine + session)

---

### Hook Points

* Cursor creation
* `execute()`, `executemany()`
* `fetchone()`, `fetchall()`
* Transaction commit / rollback

---

### SQL Context Propagation

* On `execute()`:

  * push SQL context (thread‑local)
  * include query hash + params
* Any MemWatch event during context window:

  * tagged with `sql_context_id`

---

### What Is Tracked

* Variables written **after fetch**
* Adapter tags mutations occurring in SQL context
* Does NOT infer semantic data flow (no ORM deep magic)

---

### Python SQL Limitations

* ORMs with lazy evaluation may escape context
* C extensions mutating buffers are opaque
* Explicit documentation required

---

# JavaScript Adapter (Node.js)

## Design Summary

* Runtime: Node.js (V8)
* Strategy: **TypedArray / ArrayBuffer backed memory**
* Watch semantics: `x = watch(bufferLike)`

---

## JS Adapter Components

### 1. Watch API

```js
let x = watch(new Int32Array(1), {
  trackThreads: true,
  trackSQL: true,
  mutationDepth: "FULL",
  name: "x"
});
```

Only buffer‑backed values are supported.

---

### 2. Native Binding Layer (N‑API)

* Obtain backing store pointer from ArrayBuffer
* Ensure buffer is **not reallocated**
* Register page with core

If buffer < 4KB:

* Either pad
* Or pack multiple watched buffers into one page (document ambiguity risk)

---

### 3. Mutation Enforcement

* All writes go through TypedArray index writes
* V8 writes hit backing store → page fault fires

No proxying of plain objects `{}`.

---

### 4. Thread Tracking (JS)

* Node worker threads supported
* Core captures OS `tid`
* Adapter annotates with:

  * `worker_threads.threadId`

---

## JavaScript SQL Tracking

### Libraries Covered (MVP)

* `mysql2`
* `pg`
* `better-sqlite3`
* `sqlite3`
* `sequelize` (partial)

---

### Hook Strategy

* Monkey‑patch client methods:

  * `query()`
  * `execute()`

---

### Context Propagation

* AsyncLocalStorage used
* SQL context stored per async execution chain
* Any MemWatch event during context:

  * tagged with query hash

---

### JS SQL Limitations

* Connection pooling async edges
* Native drivers may bypass JS layer
* Documented, non‑fatal

---

## Feature Matrix (Python & JS)

| Feature          | Python         | JS          |
| ---------------- | -------------- | ----------- |
| watch(x)         | Proxy required | Buffer only |
| Stack vars       | Relocate       | N/A         |
| Thread tracking  | Yes            | Yes         |
| SQL tracking     | Yes            | Yes         |
| Track all vars   | Partial        | No          |
| Byte‑level diffs | Yes            | Yes         |
| GC safe          | Yes            | Yes         |

---

## Known Hard Limits (Explicit)

* No transparent watching of arbitrary objects
* No implicit reassignment tracking
* No C‑extension hidden writes
* No cross‑process watching

These are **documented constraints**, not bugs.

---

## Why This Is Still Professional

* Same model used by sanitizers, debuggers, and profilers
* Correctness is enforced, not guessed
* Adapter boundaries are clean and testable

---

## Adapter Test Requirements

* Python: proxy mutation → event with correct line
* Python: SQL write → tagged event
* JS: TypedArray write → event
* JS: SQL query → tagged mutation
* Multi‑thread / worker test
