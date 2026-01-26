# 🌊 WaterCodeFlow Architecture

> **WaterCodeFlow** — Deterministic Execution Flow Recorder, Time‑Travel Debugger, and Tensor Mutation Tracker.
>
> Philosophy: *Like water, execution flows continuously, leaves fingerprints everywhere, and can always be traced backward.*

---

## 🎯 Core Objectives

1. **Deterministic Replay** – Reconstruct program state at any point in time.
2. **Fine‑Grained Diff Tracking** – Capture only what changed.
3. **Tensor Awareness** – Detect tensor mutations via fingerprints + diffs.
4. **Low Overhead Mode** – Lightweight tracing when deep capture is disabled.
5. **Causal Debugging** – Identify what change caused downstream behavior.
6. **Multi‑Language Extensible Core** – Python first, others pluggable.

---

## 🧱 System Overview

```
User Program
   │
   ▼
Instrumentation Layer
   │
   ▼
Event Stream (Structured)
   │
   ▼
State Tracker
   │
   ├── Memory Store
   ├── Tensor Tracker
   ├── File/DB Tracker (Optional)
   ▼
Delta Engine
   │
   ▼
Persistent Timeline Store
   │
   ▼
Replay Engine
   │
   ▼
Visualizer / API
```

---

## 🧩 Module Breakdown

### 1️⃣ Instrumentation Layer

**Purpose:** Capture program execution events.

**Captures:**

* Line execution
* Variable assignments
* Function calls / returns
* Branches
* Exceptions
* Tensor operations
* Optional filesystem + DB writes

**Implementation (Python):**

* `sys.settrace`
* AST rewrite (optional)
* Torch / NumPy monkey‑patch hooks

**Event Format:**

```json
{
  "timestamp": 123456,
  "thread_id": 1,
  "frame": "file.py:42",
  "event": "assign",
  "targets": ["x"],
  "value_hash": "abc123",
  "tensor_meta": null
}
```

---

### 2️⃣ Event Stream

**Purpose:** Unified internal protocol between capture and storage.

**Features:**

* Binary packed
* Append-only
* Zero-copy when possible
* Ordered by logical time

Supports batching and compression.

---

### 3️⃣ State Tracker

Maintains live state snapshots.

#### 🧠 Variable Tracker

* Tracks Python objects
* Fingerprint based (hash + shape + dtype + size)
* Small objects → full diff
* Large objects → fingerprint only

#### 🔢 Tensor Tracker

For NumPy / Torch / TF tensors:

**Metadata:**

* Shape
* Dtype
* Stride
* Device
* Storage ID

**Fingerprint:**

* Fast rolling hash (xxhash / murmur)
* Sampled blocks for large tensors

**Diff Policy:**

* Small tensor → full diff
* Medium tensor → block diff
* Large tensor → fingerprint only

#### 📁 IO Tracker (Optional)

* File writes
* SQLite transactions
* Logged as logical diffs

---

### 4️⃣ Delta Engine

**Purpose:** Compute minimal change sets.

**Strategies:**

* Primitive diff
* Dict diff
* Tensor block diff
* Structural diff

Outputs:

```
Δ(State_t → State_t+1)
```

---

### 5️⃣ Persistent Timeline Store

Append-only event + delta store.

**Storage:**

* LMDB / RocksDB
* Chunked timeline
* Index by timestamp

Supports:

* Random seek
* Partial replay
* Compression

---

### 6️⃣ Replay Engine

**Modes:**

#### ▶️ Forward Replay

* Deterministically reapply deltas

#### ⏪ Reverse Replay

* Invert deltas

#### 🔍 Slice Replay

* Replay only selected variables

#### 🧪 What‑If Replay

* Inject modified state and propagate

---

### 7️⃣ Visualizer + API

**Features:**

* Timeline scrubber
* Variable evolution view
* Tensor heat diff view
* Branch tree view
* Causal chain visualization

**API:**

```python
flow.goto(t=120)
flow.inspect("tensor_x")
flow.diff(t1, t2)
flow.replay(from_t, to_t)
```

---

## ⚙️ Execution Modes

| Mode     | Capture Level | Overhead | Use Case   |
| -------- | ------------- | -------- | ---------- |
| Lite     | Hash only     | Very low | Production |
| Balanced | Partial diffs | Medium   | Debug      |
| Deep     | Full diffs    | High     | Research   |

---

## 🧪 Tensor Mutation Handling

### Mutation Detection

* Compare fingerprint each step
* Detect silent in‑place mutations

### Diff Strategy

| Tensor Size | Storage          |
| ----------- | ---------------- |
| <100KB      | Full copy        |
| <10MB       | Block diff       |
| >10MB       | Fingerprint only |

---

## 🧬 Causal Tracing

Tracks:

* Variable dependency graph
* Tensor mutation propagation
* Line‑level causality

Supports root cause queries:

> "What changed this tensor?"

---

## 🚀 Extensibility

| Layer           | Extension          |
| --------------- | ------------------ |
| Instrumentation | New language hooks |
| Tensor          | New frameworks     |
| Storage         | New backends       |
| UI              | Plugins            |

---

## 🏗️ MVP Build Plan (20 Days)

### Week 1

* Python tracer
* Variable diff
* Timeline storage

### Week 2

* Tensor fingerprinting
* Replay engine
* Minimal UI

### Week 3

* Optimization
* Demo scenarios
* Polish

---

## 🌊 Water Principle Mapping

| Water Concept | System Mapping  |
| ------------- | --------------- |
| Flow          | Event stream    |
| Memory        | Timeline store  |
| Pressure      | Causal tracing  |
| Adaptation    | Dynamic capture |
| Reflection    | Replay          |

---

**WaterCodeFlow** is designed to make invisible execution visible, reversible, and trustworthy.
