cmake_minimum_required(VERSION 3.20)
project(RapidKV)

set(CMAKE_CXX_STANDARD 20)

# Release build optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_CONFIGURATION_TYPES)
    if(MSVC)
        add_compile_options("$<$<CONFIG:Release>:/O2;/Oi;/Ot;/GL;/fp:fast>")
        add_link_options("$<$<CONFIG:Release>:/LTCG>")
    else()
        add_compile_options("$<$<CONFIG:Release>:-O3;-march=native;-flto>")
        add_link_options("$<$<CONFIG:Release>:-flto>")
    endif()
endif()

option(BUILD_EXTRAS "Build tests and benchmarks" OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(FetchContent)

# --- ASIO (header-only) ---
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-36-0
)
FetchContent_MakeAvailable(asio)

# --- mimalloc (replace default allocator for all targets) ---
FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG        v2.2.7
)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE     ON  CACHE BOOL "" FORCE)
if(WIN32)
    set(MI_BUILD_STATIC ON  CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
else()
    set(MI_BUILD_STATIC OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT ON  CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(mimalloc)
if(WIN32)
    set_target_properties(mimalloc-static
            PROPERTIES FOLDER "Third Party/mimalloc"
    )
else()
    set_target_properties(mimalloc-obj
            PROPERTIES FOLDER "Third Party/mimalloc"
    )
endif()


# --- Google Test & Google Benchmark (optional) ---
if(BUILD_EXTRAS)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    FetchContent_Declare(
      benchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    set_target_properties(gtest gtest_main gmock gmock_main
        PROPERTIES FOLDER "Third Party/Google Test"
    )
    set_target_properties(benchmark benchmark_main
        PROPERTIES FOLDER "Third Party/Google Benchmark"
    )
endif()

if(WIN32)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

# --- Components separated into static libs ---
add_library(RapidKV_Utility STATIC
    src/utility/utility.cpp
    inc/utility/utility.h
    src/utility/linearBuffer.cpp
    inc/utility/linearBuffer.h
    inc/utility/config.h
)
target_include_directories(RapidKV_Utility PUBLIC
    inc/utility
)
set_target_properties(RapidKV_Utility PROPERTIES FOLDER "Libraries")

add_library(RapidKV_Storage STATIC
    src/storage/slabAllocator.cpp
    inc/storage/slabAllocator.h
    src/storage/compactString.cpp
    inc/storage/compactString.h
    src/storage/database.cpp
    inc/storage/database.h
    inc/storage/flatMap.h
)
target_include_directories(RapidKV_Storage PUBLIC
    inc/storage
    external/rapidhash/inc
)
target_link_libraries(RapidKV_Storage PUBLIC RapidKV_Utility)
set_target_properties(RapidKV_Storage PROPERTIES FOLDER "Libraries")

add_library(RapidKV_Protocol STATIC
    src/protocol/respParser.cpp
    inc/protocol/respParser.h
    src/protocol/respWriter.cpp
    inc/protocol/respWriter.h
)
target_include_directories(RapidKV_Protocol PUBLIC
    inc/protocol
)
target_link_libraries(RapidKV_Protocol PUBLIC RapidKV_Utility)
set_target_properties(RapidKV_Protocol PROPERTIES FOLDER "Libraries")

add_library(RapidKV_Commands STATIC
    src/commands/commandDispatcher.cpp
    inc/commands/commandDispatcher.h
    src/commands/commands.cpp
    inc/commands/commands.h
)
target_include_directories(RapidKV_Commands PUBLIC
    inc/commands
)
target_link_libraries(RapidKV_Commands PUBLIC RapidKV_Storage RapidKV_Protocol)
set_target_properties(RapidKV_Commands PROPERTIES FOLDER "Libraries")

# --- Main RapidKV executable ---
add_executable(RapidKV
    src/main.cpp
    src/server/server.cpp
    inc/server/server.h
    src/server/connection.cpp
    inc/server/connection.h
    src/server/shard.cpp
    inc/server/shard.h
)

target_include_directories(RapidKV PRIVATE
    ${asio_SOURCE_DIR}/asio/include
    external/libdivide/inc
    inc/server
)

if(WIN32)
    set(MIMALLOC_TARGET mimalloc-static)
else()
    set(MIMALLOC_TARGET mimalloc-obj)
endif()

target_link_libraries(RapidKV PRIVATE
    ${MIMALLOC_TARGET}
    RapidKV_Commands
    RapidKV_Protocol
    RapidKV_Storage
    RapidKV_Utility
)

if(BUILD_EXTRAS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif()
