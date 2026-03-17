#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# 1. Setup Directories
BUILD_DIR="build"

# 2. Cleanup (Optional: uncomment if you want a totally fresh build every time)
# rm -rf $BUILD_DIR

# 3. Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# 4. Configure with CMake (Force Debug mode for debugging)
echo "--- Configuring Project (Debug Mode) ---"
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 5. Build the Project
echo "--- Compiling with multiple Threads ---"
make -j$(nproc)

# 6. move back to the root of project because the project needs to read config.toml file
cd ..

# 7. Run the Benchmark
echo "--- running the RateLimiter ---"
if [ -f "./build/RateLimiterServer" ]; then
    ./build/RateLimiterServer
else
    echo "Error: RateLimiterServer binary not found!"
    exit 1
fi
