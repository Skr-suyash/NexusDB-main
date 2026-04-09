cmake_minimum_required(VERSION 3.14)
project(MosaicDB VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(mosaicdb_lib STATIC
    src/storage/wal.cpp
    src/storage/memtable.cpp
    src/engine/storage_engine.cpp
    src/query/query.cpp
)
target_include_directories(mosaicdb_lib PUBLIC include)

if(WIN32)
    target_link_libraries(mosaicdb_lib PUBLIC ws2_32)
endif()

add_executable(mosaicdb_cli client/cli.cpp)
target_link_libraries(mosaicdb_cli PRIVATE mosaicdb_lib)

enable_testing()

add_executable(test_wal tests/test_wal.cpp)
target_link_libraries(test_wal PRIVATE mosaicdb_lib)
add_test(NAME WALTests COMMAND test_wal)

add_executable(test_memtable tests/test_memtable.cpp)
target_link_libraries(test_memtable PRIVATE mosaicdb_lib)
add_test(NAME MemTableTests COMMAND test_memtable)

add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE mosaicdb_lib)
add_test(NAME EngineTests COMMAND test_engine)
