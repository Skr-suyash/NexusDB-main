# MosaicDB — Detailed Working & Functioning Overview

## 1. What is MosaicDB?

MosaicDB is a **persistent key-value storage engine** written from scratch in C++17. It uses the **LSM-tree (Log-Structured Merge-tree)** architecture — the same design behind production databases like Google's LevelDB, Facebook's RocksDB, and Apache Cassandra.

It supports four operations:
- **PUT key value** — Insert or update a key-value pair
- **GET key** — Retrieve the value for a key
- **DELETE key** — Remove a key
- **SCAN start_key end_key** — Range query over sorted keys

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                      CLIENT LAYER                       │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ CLI (C++)   │  │ TCP Server   │  │ Python Client │  │
│  │ (embedded)  │  │ (port 7690)  │  │ (TCP client)  │  │
│  └──────┬──────┘  └──────┬───────┘  └───────┬───────┘  │
│         │                │                   │          │
│         ▼                ▼                   │          │
│  ┌─────────────┐  ┌─────────────┐            │          │
│  │Query Parser │  │Binary Proto │◄───────────┘          │
│  └──────┬──────┘  └──────┬──────┘                       │
└─────────┼────────────────┼──────────────────────────────┘
          │                │
          ▼                ▼
┌─────────────────────────────────────────────────────────┐
│                   STORAGE ENGINE                        │
│                                                         │
│  ┌──────────┐    ┌──────────┐    ┌──────────────────┐   │
│  │   WAL    │    │ MemTable │    │    SSTables       │   │
│  │(on disk) │    │(in RAM)  │    │   (on disk)       │   │
│  └──────────┘    └──────────┘    │  ┌─────────────┐  │   │
│                                  │  │Bloom Filter │  │   │
│                                  │  │Sparse Index │  │   │
│                                  │  └─────────────┘  │   │
│                                  └──────────────────┘   │
│                                                         │
│  ┌──────────────────┐                                   │
│  │ Compaction Engine │                                  │
│  └──────────────────┘                                   │
└─────────────────────────────────────────────────────────┘
```

---

## 3. How Each Component Works

### 3.1 Write-Ahead Log (WAL)

**Files:** `include/mosaicdb/wal.h`, `src/storage/wal.cpp`

**Purpose:** Guarantees durability — if the process crashes, no acknowledged writes are lost.

**How it works:**

1. Every PUT or DELETE operation is first written to the WAL file (`wal.log`) on disk
2. Each WAL record has this binary format:
   ```
   [CRC32 checksum - 4 bytes]
   [Operation type  - 1 byte: 0x01=PUT, 0x02=DELETE]
   [Key size        - 4 bytes, little-endian]
   [Value size      - 4 bytes, little-endian]
   [Key bytes       - variable]
   [Value bytes     - variable]
   ```
3. After writing, `fsync()` is called to force the OS to flush data to physical disk
4. On startup, `recover()` reads all WAL records, verifying each CRC32 checksum. If a checksum doesn't match, recovery stops (partial/corrupt writes are discarded)
5. After the MemTable is successfully flushed to an SSTable, the WAL is truncated (`clear()`)

**Why CRC32?** If the process crashes mid-write, the WAL file may contain a partially written record. The CRC32 checksum detects this corruption and stops recovery at that point, ensuring only complete records are replayed.

**Platform compatibility:** Uses preprocessor macros to abstract file I/O — `_open`/`_write`/`_commit` on Windows, `open`/`write`/`fsync` on POSIX systems.

---

### 3.2 MemTable (In-Memory Table)

**Files:** `include/mosaicdb/memtable.h`, `src/storage/memtable.cpp`

**Purpose:** Fast in-memory write buffer that keeps data sorted.

**How it works:**

1. Backed by `std::map<string, optional<string>>` — an ordered red-black tree
2. **PUT operation:** Inserts/updates the key. Tracks byte size for flush decisions
3. **DELETE operation:** Stores a **tombstone** (`std::nullopt`) instead of removing the key. This is critical because the key might also exist in older SSTables — the tombstone signals "this key has been deleted"
4. **GET operation:** Returns one of three states:
   - `{found=true, is_tombstone=false, value}` — key exists with a value
   - `{found=true, is_tombstone=true}` — key was deleted (stop searching SSTables)
   - `{found=false}` — key not in MemTable (continue searching SSTables)
5. **SCAN operation:** Uses `std::map::lower_bound()` and `upper_bound()` for efficient range queries
6. **Flush trigger:** When `current_size_ >= 4MB` (configurable), `should_flush()` returns true

**Concurrency model:** Uses `std::shared_mutex` —
- Multiple threads can read simultaneously (shared lock)
- Only one thread can write at a time (exclusive lock)

**Why `std::map` and not a skip list?** Simpler implementation. Production LSM-trees (like LevelDB) use skip lists for better concurrent performance, but `std::map` provides the same O(log n) sorted operations.

---

### 3.3 SSTable (Sorted String Table)

**Files:** `include/mosaicdb/sstable.h`, `src/storage/sstable.cpp`

**Purpose:** Immutable, sorted, on-disk storage of key-value pairs. This is where data lives long-term.

**SSTable file format:**

```
┌─────────────────────────────────────────────────────┐
│                    DATA SECTION                      │
│                                                     │
│  For each entry:                                    │
│  ┌─────────────┬─────────────┬──────┬───────┐       │
│  │key_size (4B)│val_size (4B)│ key  │ value │       │
│  └─────────────┴─────────────┴──────┴───────┘       │
│  (tombstones use val_size = 0xFFFFFFFF, no value)   │
│                                                     │
├─────────────────────────────────────────────────────┤
│                  BLOOM FILTER SECTION                │
│  ┌───────────────┬────────────────┬──────────────┐  │
│  │num_bits (4B)  │num_hashes (4B) │ bit array    │  │
│  └───────────────┴────────────────┴──────────────┘  │
│                                                     │
├─────────────────────────────────────────────────────┤
│                 SPARSE INDEX SECTION                 │
│  Every 16th entry gets an index record:             │
│  ┌─────────────┬───────────┬──────┐                 │
│  │key_size (4B)│offset (8B)│ key  │                 │
│  └─────────────┴───────────┴──────┘                 │
│                                                     │
├─────────────────────────────────────────────────────┤
│                  FOOTER (32 bytes)                   │
│  ┌───────────────────┬──────────────────┐           │
│  │bloom_offset  (8B) │bloom_size   (4B) │           │
│  │index_offset  (8B) │num_idx_entries(4B│           │
│  │num_entries   (4B) │MAGIC 0x4D534454  │           │
│  └───────────────────┴──────────────────┘           │
└─────────────────────────────────────────────────────┘
```

**Writing an SSTable (`SSTableWriter::build()`):**

1. Takes a sorted vector of key-value pairs from the MemTable
2. Writes each entry sequentially to the data section
3. Every 16th entry, records its file offset in the sparse index
4. Adds every key to a bloom filter
5. Writes the bloom filter, then the sparse index, then the 32-byte footer
6. The footer contains offsets to locate the bloom filter and index sections

**Reading / Querying (`SSTableReader`):**

1. **Opening:** Reads the footer (last 32 bytes) → loads bloom filter → loads sparse index → scans data to find min/max keys
2. **Point lookup (`get(key)`):**
   - Quick reject: if key < min_key or key > max_key, return NOT_FOUND
   - Bloom filter check: if the bloom filter says "definitely not here," return NOT_FOUND
   - Binary search on sparse index to find the right block
   - Linear scan within the block to find the exact key
3. **Range scan (`scan(start, end)`):** Binary search to find the starting block, then sequentially read entries until past end_key

**Naming convention:** SSTable files are named `sst_000000.db`, `sst_000001.db`, etc. Higher sequence numbers = newer data.

---

### 3.4 Bloom Filter

**Files:** `include/mosaicdb/bloom_filter.h`, `src/index/bloom_filter.cpp`

**Purpose:** A space-efficient probabilistic data structure that answers "is this key possibly in this SSTable?" It can have false positives but never false negatives.

**How it works:**

1. A bit array of `num_bits` bits (default: 10 bits per key, minimum 64 bits)
2. Uses **double hashing** with two independent hash functions:
   - **FNV-1a** (h1): `hash = 2166136261; for each byte: hash ^= byte; hash *= 16777619`
   - **MurmurHash variant** (h2): multiplicative mixing with constant `0x5bd1e995`
3. For each key, computes 7 hash positions: `pos_i = (h1 + i * h2) % num_bits`
4. **Adding a key:** Sets all 7 bit positions to 1
5. **Checking a key:** If ALL 7 bit positions are 1, returns "possibly yes." If ANY is 0, returns "definitely no"

**Performance:** With 10 bits/key and 7 hashes, the false positive rate is approximately **0.82%**. This means ~99% of unnecessary disk reads for absent keys are avoided.

---

### 3.5 Compaction Engine

**Files:** `include/mosaicdb/compaction.h`, `src/compaction/compaction.cpp`

**Purpose:** Reduces the number of SSTable files by merging them, which improves read performance and reclaims space from deleted keys.

**How it works:**

1. Triggered when the number of SSTables reaches the threshold (≥ 4)
2. Reads ALL entries from all SSTables using `read_all_entries()`
3. Merges them into a `std::map` — older SSTables are processed first, so newer values override older ones (last-writer-wins)
4. Tombstones (deleted keys) are **dropped** during compaction — they've served their purpose of shadowing older values
5. The merged result is written as a single new SSTable
6. Old SSTable files are deleted from disk

**Example:**
```
Before compaction:
  sst_000000.db: {a=1, b=2, c=3}
  sst_000001.db: {b=DELETED, d=4}
  sst_000002.db: {a=10, e=5}
  sst_000003.db: {f=6}

After compaction:
  sst_000004.db: {a=10, c=3, d=4, e=5, f=6}
  (b was deleted, a was updated to 10)
```

---

### 3.6 Storage Engine (The Orchestrator)

**Files:** `include/mosaicdb/storage_engine.h`, `src/engine/storage_engine.cpp`

**Purpose:** The central coordinator that exposes the `put/get/scan/remove` API and manages all subsystems.

**Startup sequence:**

```
1. Create data directory if it doesn't exist
2. Scan for existing SSTable files (sst_*.db)
   - Validate each file's magic number
   - Sort by sequence number (newest first)
   - Load SSTableReader for each valid file
3. Open/create the WAL file
4. Create a fresh MemTable
5. Replay WAL records into the MemTable (crash recovery)
6. If MemTable is already full, flush it immediately
```

**PUT operation flow:**

```
put("user:123", "Alice")
  │
  ├─1→ Acquire write_mutex_
  ├─2→ WAL.append(PUT, "user:123", "Alice")  ← durability first
  ├─3→ MemTable.put("user:123", "Alice")     ← then in-memory
  ├─4→ if MemTable.should_flush():
  │      ├─ entries = MemTable.dump_sorted()
  │      ├─ SSTableWriter::build(entries) → sst_000005.db
  │      ├─ Open SSTableReader for the new file
  │      ├─ Insert at front of sstables_ vector
  │      ├─ MemTable.clear()
  │      ├─ WAL.clear()
  │      └─ if sstables_.size() >= 4: maybe_compact()
  └─5→ Release write_mutex_
```

**GET operation flow:**

```
get("user:123")
  │
  ├─1→ MemTable.get("user:123")
  │      ├─ Found + has value → return OK + value      ✓ DONE
  │      ├─ Found + tombstone → return NOT_FOUND       ✓ DONE
  │      └─ Not found → continue searching...
  │
  ├─2→ For each SSTable (newest → oldest):
  │      ├─ SSTable.get("user:123")
  │      │    ├─ Key out of [min_key, max_key] range? → skip
  │      │    ├─ BloomFilter.possibly_contains()? → if no, skip
  │      │    ├─ Binary search sparse index → find block
  │      │    └─ Linear scan block → find exact key
  │      ├─ Found + has value → return OK + value      ✓ DONE
  │      ├─ Found + tombstone → return NOT_FOUND       ✓ DONE
  │      └─ Not found → try next SSTable
  │
  └─3→ Exhausted all SSTables → return NOT_FOUND
```

**SCAN operation flow:**

```
scan("a", "d")
  │
  ├─1→ Initialize merged map (ordered)
  ├─2→ For each SSTable (oldest → newest):
  │      └─ SSTable.scan("a", "d") → merge results into map
  │         (newer values overwrite older ones)
  ├─3→ MemTable.scan("a", "d") → merge into map
  │      (MemTable data overrides everything)
  ├─4→ Filter out tombstones from merged map
  └─5→ Return sorted key-value pairs
```

**Thread safety:** All write operations (PUT, DELETE) are serialized via `std::mutex write_mutex_`. The MemTable additionally uses `std::shared_mutex` internally, so concurrent GET/SCAN reads don't block each other.

---

### 3.7 TCP Server & Binary Protocol

**Files:** `include/mosaicdb/server.h`, `src/network/server.cpp`

**Purpose:** Exposes MosaicDB over the network as a TCP server.

**Server architecture:**
- Main thread: accepts incoming TCP connections
- Each client gets a **dedicated handler thread**
- Default port: **7690**
- Cross-platform: Winsock2 on Windows, POSIX sockets on Linux/macOS

**Binary protocol — Request format:**
```
Byte 0:        Command type (0x01=PUT, 0x02=GET, 0x03=DELETE, 0x04=SCAN)
Bytes 1-4:     Key size (uint32, little-endian)
Bytes 5-8:     Value size (uint32, little-endian)
Bytes 9+:      Key bytes, then Value bytes
```

**Binary protocol — Response format:**
```
Byte 0:        Status (0x00=OK, 0x01=NOT_FOUND, 0x02=ERROR)
Bytes 1-4:     Response value size (uint32, little-endian)
Bytes 5+:      Response value bytes
```

**SCAN response special format:**
```
[status=OK] [total_size]
  [count - 4 bytes]
  For each result:
    [key_size - 4 bytes] [val_size - 4 bytes] [key] [value]
```

**Safety limits:** Keys > 10 MB or values > 100 MB cause the connection to be dropped.

---

### 3.8 Query Parser

**Files:** `include/mosaicdb/query.h`, `src/query/query.cpp`

**Purpose:** Parses human-readable text commands into structured `Command` objects for the CLI.

**Supported syntax:**
```
PUT mykey myvalue       → {type: PUT, key: "mykey", value: "myvalue"}
GET mykey               → {type: GET, key: "mykey"}
DELETE mykey            → {type: DELETE_CMD, key: "mykey"}
SCAN startkey endkey    → {type: SCAN, key: "startkey", end_key: "endkey"}
```

Commands are case-insensitive (converted to uppercase before matching).

---

## 4. Client Applications

### 4.1 C++ CLI (`client/cli.cpp`)

- **Embedded mode** — directly creates a `StorageEngine` instance (no network needed)
- Provides an interactive REPL with `MosaicDB>` prompt
- Uses `QueryParser` to interpret text commands
- Data directory defaults to `mosaicdb_data/` (configurable via command-line arg)

### 4.2 TCP Server (`client/server_main.cpp`)

- Creates a `StorageEngine` + `Server`, starts listening
- Command-line flags: `--port PORT`, `--data DIR`
- Handles SIGINT/SIGTERM for graceful shutdown

### 4.3 Python Client (`client/mosaic_client.py`)

- `MosaicDBClient` class with `put()`, `get()`, `delete()`, `scan()` methods
- Speaks the same binary TCP protocol as the C++ server
- Supports context manager (`with MosaicDBClient() as client:`)
- Has a built-in interactive REPL mode when run as a script

---

## 5. Data Persistence & Recovery

### On-Disk Structure
```
mosaicdb_data/
├── wal.log           ← Write-Ahead Log (active writes)
├── sst_000000.db     ← SSTable (oldest)
├── sst_000001.db     ← SSTable
├── sst_000002.db     ← SSTable (newest)
└── ...
```

### Crash Recovery Process

1. **Load SSTables:** Scan the data directory for `sst_*.db` files, validate magic numbers, open readers
2. **Replay WAL:** Read `wal.log`, verify CRC32 for each record, apply valid records to MemTable
3. **Resume:** The engine is now in the exact same state as before the crash (minus any unacknowledged writes that failed CRC validation)

---

## 6. Build & Run

### Build with batch script (Windows):
```bash
build.bat
```

### Produces:
```
build/mosaicdb_cli.exe       # Interactive CLI (embedded engine)
build/mosaicdb_server.exe    # TCP server
build/test_*.exe             # Unit tests
```

### Run the CLI:
```bash
build/mosaicdb_cli.exe [data_directory]
```

### Run the server:
```bash
build/mosaicdb_server.exe --port 7690 --data mosaicdb_data
```

### Connect with Python client:
```bash
python client/mosaic_client.py --host 127.0.0.1 --port 7690
```

---

## 7. Test Suite

| Test | Component | What it verifies |
|------|-----------|-----------------|
| `test_wal.cpp` | WAL | Append, recovery, CRC integrity, truncation |
| `test_memtable.cpp` | MemTable | Put/get/remove, tombstones, scan, flush threshold |
| `test_bloom.cpp` | Bloom Filter | Insertion, membership, false positive rate |
| `test_sstable.cpp` | SSTable | Write/read, point lookup, scan, tombstones, validation |
| `test_compaction.cpp` | Compaction | Multi-SSTable merge, tombstone removal, dedup |
| `test_engine.cpp` | Storage Engine | End-to-end put/get/delete, WAL recovery, flush |
| `test_network.py` | TCP Server | Network protocol integration tests |

---

## 8. Design Decisions Summary

| Decision | Rationale |
|----------|-----------|
| LSM-tree architecture | Optimized for write-heavy workloads; sequential disk writes |
| WAL + fsync per write | Strong durability guarantee at the cost of write latency |
| `std::map` for MemTable | Simple sorted container; production systems use skip lists |
| Sparse index (every 16th key) | Balances index size vs lookup speed |
| Bloom filter (10 bits/key, 7 hashes) | ~1% false positive rate; avoids most unnecessary disk reads |
| Full compaction at 4 SSTables | Simple strategy; production systems use leveled compaction |
| Thread-per-client server | Simple concurrency; production systems use thread pools or async I/O |
| Little-endian serialization | Matches x86/x64 native byte order for zero-cost encoding on most hardware |
| Custom binary protocol | Minimal overhead vs text protocols like HTTP; designed for low-latency |
