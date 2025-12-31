# --- STAGE 1: Build Environment (Debian) ---
FROM debian:bookworm-slim AS builder

# Install build tools and library dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    libhiredis-dev \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    pkg-config

# Install redis-plus-plus
RUN git clone https://github.com/sewenew/redis-plus-plus.git && \
    cd redis-plus-plus && mkdir build && cd build && \
    cmake -DREDIS_PLUS_PLUS_CXX_STANDARD=20 .. && \
    make -j$(nproc) && make install

# Install tomlplusplus (Force it to build and install properly)
RUN git clone https://github.com/marzer/tomlplusplus.git && \
    cd tomlplusplus && mkdir build && cd build && \
    cmake -DTOML_BUILD_EXAMPLES=OFF -DTOML_BUILD_TESTS=OFF .. && \
    make -j$(nproc) && make install
# Build your Application
WORKDIR /app
COPY . .
RUN mkdir Build && cd Build && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. && \
    ninja

# --- STAGE 2: Runtime ---
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libhiredis-dev \
    libgrpc++1.51 \
    libprotobuf32 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/bin

COPY --from=builder /app/Build/RateLimiter .
COPY config.toml .

# Copy redis++ (this one usually works as .so)
COPY --from=builder /usr/local/lib/libredis++.so* /usr/local/lib/

# If tomlplusplus is missing, it's often because it's in /usr/local/include 
# or linked statically. Let's try to refresh the cache without it first.
RUN ldconfig

EXPOSE 50051
CMD ["./RateLimiter"]
