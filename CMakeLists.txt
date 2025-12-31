cmake_minimum_required(VERSION 3.10)
project(RateLimiter)

# --- 1. C++ Standard ---
# Using C++20 for modern features like std::chrono improvements
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 2. Custom Find Scripts ---
# Tells CMake to look in your 'cmake' folder for Findhiredis.cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- 3. Find Dependencies (Installed via pacman) ---
find_package(hiredis REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# --- 4. Locate Tools (The "Build Boss" Step) ---
# We find the actual paths of the compilers on your system
find_program(PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

if(NOT PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN_EXECUTABLE)
    message(FATAL_ERROR "Missing tools! Install them: sudo pacman -S grpc protobuf")
endif()

# --- 5. Define Proto Paths ---
set(proto_file "${CMAKE_CURRENT_SOURCE_DIR}/proto/ratelimit.proto")

# These variables define where the generated C++ files will live
set(GEN_PB_SRC "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.pb.cc")
set(GEN_PB_HDR "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.pb.h")
set(GEN_GRPC_SRC "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.grpc.pb.cc")
set(GEN_GRPC_HDR "${CMAKE_CURRENT_BINARY_DIR}/ratelimit.grpc.pb.h")

# --- 6. Automate Code Generation ---
# This runs 'protoc' to turn your .proto into C++ code before compiling
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

# --- 7. Define the Executable ---
# List every .cpp file here. If you add a new strategy, add its .cpp here too.
add_executable(RateLimiter 
  src/main.cpp
  src/tomlParser.cpp
  src/FixedWindow.cpp
  src/TokenBucket.cpp
  src/RateLimitFactory.cpp
  "${GEN_PB_SRC}"   # Include the generated code
  "${GEN_GRPC_SRC}" # Include the generated code
)

# --- 8. Include Directories ---
# Tells the compiler where to find your .h files
target_include_directories(RateLimiter PRIVATE 
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}" # For the generated headers
)

# --- 9. Linking (The "Glue" Step) ---
# Connects your code to the libraries
target_link_libraries(RateLimiter PRIVATE 
  redis++::redis++
  hiredis::hiredis
  gRPC::grpc++
  protobuf::libprotobuf
  tomlplusplus::tomlplusplus
)
