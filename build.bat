@echo off
setlocal

set CXX=g++
set CXXFLAGS=-std=c++17 -Wall -Wextra -O2 -Iinclude
set OUTDIR=build

if not exist %OUTDIR% mkdir %OUTDIR%

echo [1/13] Compiling storage/wal.cpp...
%CXX% %CXXFLAGS% -c src/storage/wal.cpp -o %OUTDIR%/wal.o
if errorlevel 1 goto :fail

echo [2/13] Compiling storage/memtable.cpp...
%CXX% %CXXFLAGS% -c src/storage/memtable.cpp -o %OUTDIR%/memtable.o
if errorlevel 1 goto :fail

echo [3/13] Compiling index/bloom_filter.cpp...
%CXX% %CXXFLAGS% -c src/index/bloom_filter.cpp -o %OUTDIR%/bloom_filter.o
if errorlevel 1 goto :fail

echo [4/13] Compiling storage/sstable.cpp...
%CXX% %CXXFLAGS% -c src/storage/sstable.cpp -o %OUTDIR%/sstable.o
if errorlevel 1 goto :fail

echo [5/13] Compiling compaction/compaction.cpp...
%CXX% %CXXFLAGS% -c src/compaction/compaction.cpp -o %OUTDIR%/compaction.o
if errorlevel 1 goto :fail

echo [6/13] Compiling engine/storage_engine.cpp...
%CXX% %CXXFLAGS% -c src/engine/storage_engine.cpp -o %OUTDIR%/storage_engine.o
if errorlevel 1 goto :fail

echo [7/13] Compiling query/query.cpp...
%CXX% %CXXFLAGS% -c src/query/query.cpp -o %OUTDIR%/query.o
if errorlevel 1 goto :fail

echo [8/13] Compiling network/server.cpp...
%CXX% %CXXFLAGS% -c src/network/server.cpp -o %OUTDIR%/server.o
if errorlevel 1 goto :fail

echo [9/13] Compiling relational/row.cpp...
%CXX% %CXXFLAGS% -c src/relational/row.cpp -o %OUTDIR%/row.o
if errorlevel 1 goto :fail

echo [10/13] Compiling relational/catalog.cpp...
%CXX% %CXXFLAGS% -c src/relational/catalog.cpp -o %OUTDIR%/catalog.o
if errorlevel 1 goto :fail

echo [11/13] Compiling sql/sql_tokenizer.cpp...
%CXX% %CXXFLAGS% -c src/sql/sql_tokenizer.cpp -o %OUTDIR%/sql_tokenizer.o
if errorlevel 1 goto :fail

echo [12/13] Compiling sql/sql_parser.cpp...
%CXX% %CXXFLAGS% -c src/sql/sql_parser.cpp -o %OUTDIR%/sql_parser.o
if errorlevel 1 goto :fail

echo [13/13] Compiling sql/executor.cpp...
%CXX% %CXXFLAGS% -c src/sql/executor.cpp -o %OUTDIR%/executor.o
if errorlevel 1 goto :fail

set LIBOBJS=%OUTDIR%/wal.o %OUTDIR%/memtable.o %OUTDIR%/bloom_filter.o %OUTDIR%/sstable.o %OUTDIR%/compaction.o %OUTDIR%/storage_engine.o %OUTDIR%/query.o %OUTDIR%/server.o %OUTDIR%/row.o %OUTDIR%/catalog.o %OUTDIR%/sql_tokenizer.o %OUTDIR%/sql_parser.o %OUTDIR%/executor.o

echo.
echo --- Linking binaries ---

echo [LINK] mosaicdb_cli.exe...
%CXX% %CXXFLAGS% client/cli.cpp %LIBOBJS% -o %OUTDIR%/mosaicdb_cli.exe -lws2_32
if errorlevel 1 goto :fail

echo [LINK] mosaicdb_server.exe...
%CXX% %CXXFLAGS% client/server_main.cpp %LIBOBJS% -o %OUTDIR%/mosaicdb_server.exe -lws2_32
if errorlevel 1 goto :fail

echo.
echo --- Building Tests ---

echo [TEST] test_wal...
%CXX% %CXXFLAGS% tests/test_wal.cpp %LIBOBJS% -o %OUTDIR%/test_wal.exe -lws2_32
if errorlevel 1 goto :fail

echo [TEST] test_memtable...
%CXX% %CXXFLAGS% tests/test_memtable.cpp %LIBOBJS% -o %OUTDIR%/test_memtable.exe -lws2_32
if errorlevel 1 goto :fail

echo [TEST] test_bloom...
%CXX% %CXXFLAGS% tests/test_bloom.cpp %LIBOBJS% -o %OUTDIR%/test_bloom.exe -lws2_32
if errorlevel 1 goto :fail

echo [TEST] test_sstable...
%CXX% %CXXFLAGS% tests/test_sstable.cpp %LIBOBJS% -o %OUTDIR%/test_sstable.exe -lws2_32
if errorlevel 1 goto :fail

echo [TEST] test_compaction...
%CXX% %CXXFLAGS% tests/test_compaction.cpp %LIBOBJS% -o %OUTDIR%/test_compaction.exe -lws2_32
if errorlevel 1 goto :fail

echo [TEST] test_engine...
%CXX% %CXXFLAGS% tests/test_engine.cpp %LIBOBJS% -o %OUTDIR%/test_engine.exe -lws2_32
if errorlevel 1 goto :fail

echo [TEST] test_sql...
%CXX% %CXXFLAGS% tests/test_sql.cpp %LIBOBJS% -o %OUTDIR%/test_sql.exe -lws2_32
if errorlevel 1 goto :fail

echo.
echo ===== BUILD SUCCESSFUL =====
echo Binaries in %OUTDIR%/
echo   mosaicdb_cli.exe    - Interactive SQL CLI
echo   mosaicdb_server.exe - TCP server
echo   Python client:  client/mosaic_client.py
goto :end

:fail
echo.
echo ===== BUILD FAILED =====
exit /b 1

:end
endlocal
