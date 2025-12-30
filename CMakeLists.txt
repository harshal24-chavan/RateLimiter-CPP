cmake_minimum_required(VERSION 3.10)
project(RateLimiter)

# 1. Tell CMake to look in your local 'cmake' folder for Find scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. Find hiredis FIRST using our new shim
find_package(hiredis REQUIRED)

# 3. Now find redis++. It will see that 'hiredis' is already found 
# and won't throw the error.
find_package(redis++ CONFIG REQUIRED)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

add_executable(RateLimiter 
  src/main.cpp
  src/tomlParser.cpp
  src/FixedWindowLimit.cpp
  src/RateLimitFactory.cpp
)

target_include_directories(RateLimiter PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(RateLimiter PRIVATE 
    redis++::redis++
    hiredis::hiredis
    gRPC::grpc++
    protobuf::libprotobuf
)
