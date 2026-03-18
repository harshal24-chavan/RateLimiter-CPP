cmake_minimum_required(VERSION 3.10)
project(RateLimiter)

# --- 1. C++ Standard & Performance ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization for the i3-540
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}  -march=native -pthread")

# --- 2. Custom Find Scripts ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- 3. Find Dependencies ---
find_package(hiredis REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# --- 4. Locate Protoc/gRPC Tools ---
find_program(PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

if(NOT PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN_EXECUTABLE)
    message(FATAL_ERROR "Missing tools! Install them: sudo pacman -S grpc protobuf")
endif()

# --- 5. Define Proto Paths ---
set(proto_file "${CMAKE_CURRENT_SOURCE_DIR}/proto/ratelimit.proto")
set(GEN_PB_SRC "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.pb.cc")
set(GEN_PB_HDR "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.pb.h")
set(GEN_GRPC_SRC "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.grpc.pb.cc")
set(GEN_GRPC_HDR "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.grpc.pb.h")

# --- 6. Automate Code Generation ---
add_custom_command(
    OUTPUT "${GEN_PB_SRC}" "${GEN_PB_HDR}" "${GEN_GRPC_SRC}" "${GEN_GRPC_HDR}"
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
         -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${proto_file}
    DEPENDS ${proto_file}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

# --- 7. Define the Core Library (Logic Only) ---
# We use STATIC library so multiple executables can use these objects.
# Note: src/main.cpp is EXCLUDED from the library.
add_library(rl_core STATIC
    src/tomlParser.cpp
    src/FixedWindow.cpp
    src/TokenBucket.cpp
    src/RateLimitFactory.cpp
    src/SyncManager.cpp
    src/GRPC-RateLimiter.cpp
    "${GEN_PB_SRC}"   
    "${GEN_GRPC_SRC}" 
)

# --- 8. Library Include Directories ---
target_include_directories(rl_core PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}" 
)

# --- 9. Library Linking ---
target_link_libraries(rl_core PUBLIC 
    redis++::redis++
    hiredis::hiredis
    gRPC::grpc++
    protobuf::libprotobuf
    tomlplusplus::tomlplusplus
)

# --- 10. Define the Executables ---

# Main Server (Contains its own main.cpp)
add_executable(RateLimiterServer src/main.cpp)
target_link_libraries(RateLimiterServer PRIVATE rl_core)

# Unit Tests (Contains tests/FixedWindowTests.cpp's main)
add_executable(unit_tests tests/FixedWindowTest.cpp tests/test_sync_pull.cpp)
target_link_libraries(unit_tests PRIVATE rl_core)

# Benchmark (Contains benchmarks/FixedWindowBench.cpp's main)
add_executable(bench_fixed_window benchmarks/FixedWindowBench.cpp)
target_link_libraries(bench_fixed_window PRIVATE rl_core)

# Optional: Test Client
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/client_test.cpp")
    add_executable(TestClient src/client_test.cpp)
    target_link_libraries(TestClient PRIVATE rl_core)
endif()
