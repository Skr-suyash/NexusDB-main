@echo off
setlocal

set CXX=g++
set CXXFLAGS=-std=c++17 -Wall -Wextra -O2 -Iinclude
set OUTDIR=build

if not exist %OUTDIR% mkdir %OUTDIR%

echo [1/9] Compiling storage/wal.cpp...
%CXX% %CXXFLAGS% -c src/storage/wal.cpp -o %OUTDIR%/wal.o
if errorlevel 1 goto :fail

echo [2/9] Compiling storage/memtable.cpp...
%CXX% %CXXFLAGS% -c src/storage/memtable.cpp -o %OUTDIR%/memtable.o
if errorlevel 1 goto :fail

echo [3/9] Compiling index/bloom_filter.cpp...
%CXX% %CXXFLAGS% -c src/index/bloom_filter.cpp -o %OUTDIR%/bloom_filter.o
if errorlevel 1 goto :fail

echo [4/9] Compiling storage/sstable.cpp...
%CXX% %CXXFLAGS% -c src/storage/sstable.cpp -o %OUTDIR%/sstable.o
if errorlevel 1 goto :fail

echo [5/9] Compiling compaction/compaction.cpp...
%CXX% %CXXFLAGS% -c src/compaction/compaction.cpp -o %OUTDIR%/compaction.o
if errorlevel 1 goto :fail

echo [6/9] Compiling engine/storage_engine.cpp...
%CXX% %CXXFLAGS% -c src/engine/storage_engine.cpp -o %OUTDIR%/storage_engine.o
if errorlevel 1 goto :fail

echo [7/9] Compiling query/query.cpp...
%CXX% %CXXFLAGS% -c src/query/query.cpp -o %OUTDIR%/query.o
if errorlevel 1 goto :fail

echo [8/9] Compiling network/server.cpp...
%CXX% %CXXFLAGS% -c src/network/server.cpp -o %OUTDIR%/server.o
if errorlevel 1 goto :fail

set LIBOBJS=%OUTDIR%/wal.o %OUTDIR%/memtable.o %OUTDIR%/bloom_filter.o %OUTDIR%/sstable.o %OUTDIR%/compaction.o %OUTDIR%/storage_engine.o %OUTDIR%/query.o %OUTDIR%/server.o

echo [9/9] Linking binaries...
%CXX% %CXXFLAGS% client/cli.cpp %LIBOBJS% -o %OUTDIR%/mosaicdb_cli.exe -lws2_32
if errorlevel 1 goto :fail

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

echo.
echo ===== BUILD SUCCESSFUL =====
echo Binaries in %OUTDIR%/
echo   mosaicdb_cli.exe    - Interactive CLI
echo   mosaicdb_server.exe - TCP server
echo   Python client:  client/mosaic_client.py
goto :end

:fail
echo.
echo ===== BUILD FAILED =====
exit /b 1

:end
endlocal
