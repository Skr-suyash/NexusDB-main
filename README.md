# MosaicDB

MosaicDB is a persistent key-value storage engine written in C++17 using the LSM-tree (Log-Structured Merge-tree) architecture. It is designed to be highly efficient for write-heavy workloads, ensuring durability and performance. Built on top of the storage engine is a full relational SQL layer, supported by a multi-threaded TCP server, a Python client, an HTTP proxy, and an interactive React-based visualizer.

---

## Architecture

The following diagram illustrates the flow of data from the client layer down to the on-disk storage engine:

```mermaid
flowchart TB
    %% Styling
    classDef client fill:#f9f9f9,stroke:#333,stroke-width:2px,color:#000;
    classDef network fill:#e1f5fe,stroke:#0288d1,stroke-width:2px,color:#000;
    classDef sql fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000;
    classDef storage fill:#e8f5e9,stroke:#388e3c,stroke-width:2px,color:#000;
    classDef disk fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000;

    %% Nodes
    subgraph Clients ["💻 Client Applications"]
        CLI["C++ CLI (Embedded mode)"]:::client
        PyClient["Python Client"]:::client
        Visualizer["React Visualizer\n(SQL Workbench, Crash Sim)"]:::client
    end

    subgraph Network ["🌐 Network & Proxy Layer"]
        Proxy["HTTP Proxy\nNode.js (:3001)"]:::network
        Server["TCP Server\nThread-per-client (:7690)"]:::network
        Proto["Binary Protocol\n(OpType, KeySize, ValSize, Data)"]:::network
    end

    subgraph Relational ["⚙️ Relational / SQL Layer"]
        Tokenizer["SQL Tokenizer\n(Lexer)"]:::sql
        Parser["SQL Parser\n(Recursive-descent, AST)"]:::sql
        Executor["SQL Executor\n(Schema/Catalog & Row Types)"]:::sql
    end

    subgraph Engine ["🧠 Storage Engine (LSM-Tree)"]
        API["Storage Engine API\n(put, get, scan, remove)"]:::storage
        MemTable["MemTable\nstd::map, RAM (4MB threshold)"]:::storage
        Compaction["Compaction Engine\n(Triggered >= 4 files)"]:::storage
    end

    subgraph Disk ["💾 On-Disk Persistence"]
        WAL["Write-Ahead Log (wal.log)\n(CRC32 protected)"]:::disk
        subgraph SSTables ["SSTables (Immutable files)"]
            SST["sst_XXXXXX.db\n(Sorted data blocks)"]:::disk
            Bloom["Bloom Filter\n(10 bits/key, 7 hashes)"]:::disk
            Sparse["Sparse Index\n(Every 16th key)"]:::disk
        end
    end

    %% Connections
    Visualizer -- "HTTP POST\n/api/sql" --> Proxy
    Proxy -- "TCP Socket" --> Server
    PyClient -- "TCP Socket" --> Server
    Server --> Proto
    
    Proto -- "SQL Payload" --> Tokenizer
    CLI -- "Raw SQL text" --> Tokenizer
    
    Tokenizer -- "Tokens" --> Parser
    Parser -- "AST\n(Select, Insert, etc)" --> Executor
    
    Executor -- "KV operations\n(Catalog ops, Row ser/des)" --> API
    Proto -- "Raw KV commands" --> API
    CLI -- "Raw KV commands" --> API
    
    API -- "1. Append (Durability)" --> WAL
    API -- "2. Insert / Tombstone" --> MemTable
    API -- "3. Read (MemTable first)" --> MemTable
    
    MemTable -- "Flush when full (4MB)" --> SST
    
    API -- "4. Read (Fallback)" --> Bloom
    Bloom -- "Probable match" --> Sparse
    Sparse -- "Seek offset" --> SST
    
    SST -- "Trigger (count >= 4)" --> Compaction
    Compaction -- "Merge-sort & GC" --> SST
```

---

## Features

- **LSM-Tree Storage Engine**: Sequential disk writes, optimized for high throughput.
- **Write-Ahead Log (WAL)**: Ensures strong durability; all operations are checksummed (CRC32) before being applied.
- **SSTables & MemTables**: Data is buffered in a `std::map` and flushed to immutable Sorted String Tables (SSTables) when full (4MB threshold).
- **Fast Lookups**: Employs **Bloom Filters** (10 bits/key, 7 hashes, ~0.82% false positive rate) and **Sparse Indexes** (every 16th key).
- **Relational SQL Layer**: Supports tables, types (INT, FLOAT, TEXT, BOOL), and queries (`CREATE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `WHERE`, `ORDER BY`, `LIMIT`).
- **Network Protocol**: Multi-threaded TCP server with a lightweight, little-endian custom binary protocol.
- **Client Ecosystem**: Python client for automation, an embedded C++ interactive CLI, and a Node.js proxy bridge.
- **Interactive Visualizer**: A rich React app to visualize LSM-tree compactions, simulate crashes/recovery, and provide a live SQL workbench.

---

## Getting Started

### Prerequisites
- C++17 Compiler (e.g., MSVC on Windows)
- Python 3.x (for the Python client)
- Node.js & npm (for the proxy and visualizer)

### Build (Windows)
Run the build script to compile the storage engine, server, and CLI tools:
```bat
build.bat
```
This produces `mosaicdb_cli.exe`, `mosaicdb_server.exe`, and unit test binaries in the `build/` folder.

### Run

**1. Start the Database Server:**
```bat
build\mosaicdb_server.exe --port 7690 --data mosaicdb_data
```

**2. Connect with a Client:**
Use the included Python client to connect:
```bat
python client\mosaic_client.py --host 127.0.0.1 --port 7690
```

**3. Launch the Visualizer (Optional):**
To use the web-based SQL workbench and visualization tools, start the HTTP proxy and React app:
```bat
:: Start the Proxy
node visualizer\proxy.mjs

:: In a new terminal, start the React App
cd visualizer
npm install
npm run dev
```

---

## Code Structure

- `include/mosaicdb/` / `src/storage/`: Core engine (WAL, MemTable, SSTable, Compaction)
- `include/mosaicdb/` / `src/sql/`: Relational layer (Tokenizer, Parser, Executor, Catalog)
- `src/network/`: Multi-threaded TCP Server
- `client/`: C++ CLI embedded interface and Python TCP Client
- `visualizer/`: Node.js Proxy bridge and the React + Vite web application

---

## Testing

The project includes an extensive test suite verifying everything from WAL integrity to network behavior. Test executables (e.g. `test_wal.exe`, `test_sql.exe`) are output to the `build/` directory. You can also run the integration tests using the Python test script (`test_network.py`).

## Images

|  |  |
|---|---|
| <img width="600" src="https://github.com/user-attachments/assets/55f28767-c9b4-4270-b42c-8b016d545ea8" /> | <img width="400" src="https://github.com/user-attachments/assets/aad59813-cf1f-483a-a7ad-f9fecd1a04d8" /> |
| <img width="600" src="https://github.com/user-attachments/assets/16590f2d-d8a5-44b1-993c-7a68c1f4697e" /> | <img width="400" src="https://github.com/user-attachments/assets/f20e58ea-3ee5-4aac-aa54-ef35247173f9" /> |
