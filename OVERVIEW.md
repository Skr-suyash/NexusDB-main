# MosaicDB -- Overview

MosaicDB is a persistent key-value storage engine written in C++17 using the LSM-tree (Log-Structured Merge-tree) architecture. It includes a full relational SQL layer, a TCP server with a custom binary protocol, a Python client, an HTTP proxy, and a React-based visualizer.

---

## Architecture

```
+-----------------------------------------------------------------+
|                        CLIENT LAYER                             |
|  +-------------+  +--------------+  +----------+  +----------+ |
|  | C++ CLI     |  | TCP Server   |  | Python   |  | HTTP     | |
|  | (embedded)  |  | (port 7690)  |  | Client   |  | Proxy    | |
|  +------+------+  +------+-------+  +----+-----+  +----+-----+ |
|         |                |                |             |       |
|         v                v                |             |       |
|  +-------------+  +-------------+         |             |       |
|  | SQL Parser  |  | Binary Proto|<--------+             |       |
|  | + Executor  |  | (PROTO_SQL) |<----------------------+       |
|  +------+------+  +------+------+                               |
+---------+----------------+--------------------------------------+
          |                |
          v                v
+-----------------------------------------------------------------+
|                     STORAGE ENGINE                              |
|                                                                 |
|  +----------+    +----------+    +--------------------+         |
|  |   WAL    |    | MemTable |    |     SSTables       |         |
|  | (wal.log)|    | (std::map|    |  +---------------+ |         |
|  +----------+    |  in RAM) |    |  | Bloom Filter  | |         |
|                  +----------+    |  | Sparse Index  | |         |
|                                  |  +---------------+ |         |
|  +------------------+           +--------------------+         |
|  | Compaction Engine |                                          |
|  +------------------+                                          |
+-----------------------------------------------------------------+
          |
          v
+-----------------------------------------------------------------+
|                    VISUALIZER (React + Vite)                    |
|  +-------------------+  +------------------+  +-------------+  |
|  | Compaction Matrix  |  | Crash & Recovery |  | SQL         |  |
|  | (LSM-tree anim.)  |  | Simulator (WAL)  |  | Workbench   |  |
|  +-------------------+  +------------------+  +-------------+  |
+-----------------------------------------------------------------+
```

---

## Components

### Write-Ahead Log (WAL)

**Files:** `include/mosaicdb/wal.h`, `src/storage/wal.cpp`

Guarantees durability. Every PUT/DELETE is written to `wal.log` before updating the MemTable. Each record has a CRC32 checksum to detect partial writes from crashes. On startup, valid records are replayed into the MemTable; corrupt records are discarded.

Record format: `[CRC32 4B] [op_type 1B] [key_size 4B] [val_size 4B] [key] [value]`

### MemTable

**Files:** `include/mosaicdb/memtable.h`, `src/storage/memtable.cpp`

An in-memory sorted buffer backed by `std::map<string, optional<string>>`. Deletions are stored as tombstones (`std::nullopt`) to shadow values in older SSTables. Flushes to an SSTable when size reaches 4 MB. Uses `std::shared_mutex` for concurrent reads.

### SSTable (Sorted String Table)

**Files:** `include/mosaicdb/sstable.h`, `src/storage/sstable.cpp`

Immutable on-disk sorted files. Each SSTable contains a data section, a Bloom filter, a sparse index (every 16th key), and a 32-byte footer with magic number `0x4D534454`.

Point lookups use: min/max key range check, then Bloom filter, then binary search on sparse index, then linear scan within the block.

Files are named `sst_000000.db`, `sst_000001.db`, etc. Higher sequence = newer data.

### Bloom Filter

**Files:** `include/mosaicdb/bloom_filter.h`, `src/index/bloom_filter.cpp`

Probabilistic data structure using double hashing (FNV-1a + MurmurHash variant) with 10 bits/key and 7 hash functions. False positive rate is approximately 0.82%. Never produces false negatives.

### Compaction Engine

**Files:** `include/mosaicdb/compaction.h`, `src/compaction/compaction.cpp`

Triggered when SSTable count reaches 4 or more. Merges all SSTables into one: newer values overwrite older ones, tombstones are dropped. Old files are deleted.

### Storage Engine

**Files:** `include/mosaicdb/storage_engine.h`, `src/engine/storage_engine.cpp`

The central orchestrator. Exposes `put()`, `get()`, `scan()`, `remove()`.

- **PUT:** WAL append -> MemTable insert -> flush to SSTable if full -> compact if needed
- **GET:** Check MemTable first -> search SSTables newest-to-oldest (Bloom filter + sparse index)
- **SCAN:** Merge results from all SSTables (oldest-first) + MemTable, filter tombstones
- **Startup:** Load existing SSTables -> replay WAL -> resume

All writes are serialized via `std::mutex`. Reads use shared locks.

---

## Relational / SQL Layer

Built on top of the KV engine. Tables are stored as KV entries:
- Schemas: key = `__catalog__:tablename`, value = serialized `TableSchema`
- Rows: key = `tablename:pk_value`, value = serialized row bytes

### Type System

**File:** `include/mosaicdb/types.h`

Supports INT, FLOAT, TEXT, and BOOL column types. The `Value` struct provides comparison operators (`compare_eq`, `compare_lt`, `compare_gt`) and NULL handling.

### Schema and Catalog

**Files:** `include/mosaicdb/schema.h`, `include/mosaicdb/catalog.h`, `src/relational/catalog.cpp`

`TableSchema` holds column definitions (name, type, NOT NULL, PRIMARY KEY) and serializes/deserializes to binary for KV storage. The `Catalog` manages schema CRUD operations.

### Row Serialization

**Files:** `include/mosaicdb/row.h`, `src/relational/row.cpp`

Serializes a vector of `Value` objects into a binary string for KV storage and deserializes them back.

### SQL Tokenizer

**Files:** `include/mosaicdb/sql_tokenizer.h`, `src/sql/sql_tokenizer.cpp`

Lexer that breaks SQL text into tokens (keywords, identifiers, literals, operators, punctuation).

### SQL Parser

**Files:** `include/mosaicdb/sql_parser.h`, `src/sql/sql_parser.cpp`

Recursive-descent parser. Converts tokens into an AST using `std::variant<CreateTableStmt, InsertStmt, SelectStmt, ...>`. Supports WHERE clauses with AND/OR logic, ORDER BY, and LIMIT.

### Executor

**Files:** `include/mosaicdb/executor.h`, `src/sql/executor.cpp`

Executes parsed SQL statements against the storage engine via the catalog and row layer. Returns `QueryResult` with status message, column names, and row data.

**Supported SQL:**
- `CREATE TABLE name (col1 INT PRIMARY KEY, col2 TEXT NOT NULL, ...)`
- `INSERT INTO name (col1, col2) VALUES (1, 'text')`
- `SELECT col1, col2 FROM name WHERE col1 > 5 ORDER BY col2 LIMIT 10`
- `UPDATE name SET col1 = value WHERE condition`
- `DELETE FROM name WHERE condition`
- `DROP TABLE [IF EXISTS] name`
- `SHOW TABLES`
- `DESCRIBE name`

---

## TCP Server and Binary Protocol

**Files:** `include/mosaicdb/server.h`, `src/network/server.cpp`, `client/server_main.cpp`

Multi-threaded TCP server (thread-per-client) on default port 7690. Binds on `INADDR_ANY`. Cross-platform (Winsock2 on Windows, POSIX sockets on Linux/macOS).

**Request format:** `[type 1B] [key_size 4B LE] [val_size 4B LE] [key bytes] [value bytes]`

Command types: `0x01`=PUT, `0x02`=GET, `0x03`=DELETE, `0x04`=SCAN, `0x05`=SQL

**Response format:** `[status 1B] [val_size 4B LE] [value bytes]`

Status codes: `0x00`=OK, `0x01`=NOT_FOUND, `0x02`=ERROR

SQL responses encode: message + column names + rows, all length-prefixed with 4-byte LE integers.

---

## Client Applications

### C++ CLI (`client/cli.cpp`)

Embedded mode -- directly instantiates the storage engine (no network). Interactive REPL with `MosaicSQL>` prompt.

### Python Client (`client/mosaic_client.py`)

Standalone TCP client. Supports both KV commands and SQL queries. Can be used as a library or run as an interactive REPL.

```
python mosaic_client.py --host 127.0.0.1 --port 7690
```

---

## HTTP Proxy (`visualizer/proxy.mjs`)

Node.js bridge that converts browser HTTP requests to MosaicDB TCP protocol. Runs on port 3001.

- `POST /api/sql` -- accepts `{"sql": "..."}`, forwards to TCP server, returns JSON result
- `GET /api/health` -- checks if MosaicDB server is reachable

```
node visualizer/proxy.mjs
```

---

## Visualizer (`visualizer/`)

React + TypeScript + Vite application with three interactive views:

- **Compaction Matrix** -- animates LSM-tree merge-sort and conflict resolution
- **Crash and Recovery Simulator** -- demonstrates WAL-based durability and corruption recovery
- **SQL Workbench** -- live SQL editor connected to the engine via the HTTP proxy

```
cd visualizer
npm install
npm run dev
```

---

## Build and Run

### Build (Windows):
```
build.bat
```

Produces:
- `build/mosaicdb_cli.exe` -- Interactive SQL CLI
- `build/mosaicdb_server.exe` -- TCP server
- `build/test_*.exe` -- Unit tests

### Run:
```
:: Start the server
build\mosaicdb_server.exe --port 7690 --data mosaicdb_data

:: Connect with Python client
python client\mosaic_client.py --host 127.0.0.1 --port 7690

:: Start the visualizer (requires server + proxy)
node visualizer\proxy.mjs
cd visualizer && npm run dev
```

---

## On-Disk Structure

```
mosaicdb_data/
  wal.log           -- Write-Ahead Log
  sst_000000.db     -- SSTable (oldest)
  sst_000001.db     -- SSTable
  sst_000002.db     -- SSTable (newest)
```

---

## Test Suite

| Test | Component | What it verifies |
|------|-----------|-----------------|
| `test_wal.cpp` | WAL | Append, recovery, CRC integrity, truncation |
| `test_memtable.cpp` | MemTable | Put/get/remove, tombstones, scan, flush threshold |
| `test_bloom.cpp` | Bloom Filter | Insertion, membership, false positive rate |
| `test_sstable.cpp` | SSTable | Write/read, point lookup, scan, tombstones |
| `test_compaction.cpp` | Compaction | Multi-SSTable merge, tombstone removal, dedup |
| `test_engine.cpp` | Storage Engine | End-to-end put/get/delete, WAL recovery, flush |
| `test_sql.cpp` | SQL Layer | CREATE, INSERT, SELECT, UPDATE, DELETE, schema ops |
| `test_network.py` | TCP Server | Network protocol integration tests |

---

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| LSM-tree architecture | Optimized for write-heavy workloads; sequential disk writes |
| WAL + fsync per write | Strong durability guarantee |
| `std::map` for MemTable | Simple sorted container; production systems use skip lists |
| Sparse index (every 16th key) | Balances index size vs lookup speed |
| Bloom filter (10 bits/key, 7 hashes) | ~1% false positive rate; avoids unnecessary disk reads |
| Full compaction at 4 SSTables | Simple strategy; production systems use leveled compaction |
| Thread-per-client server | Simple concurrency model |
| Little-endian serialization | Matches x86/x64 native byte order |
| Custom binary protocol | Minimal overhead vs text protocols |
| SQL on KV via prefix keys | Schemas stored as `__catalog__:name`, rows as `table:pk` |
| TCP-to-HTTP proxy | Bridges browser (no raw TCP) to the engine via Node.js |
