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

# Install tomlplusplus
RUN git clone https://github.com/marzer/tomlplusplus.git && \
    cd tomlplusplus && mkdir build && cd build && \
    cmake .. && make -j$(nproc) && make install

# Build your Application
WORKDIR /app
COPY . .
RUN mkdir Build && cd Build && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. && \
    ninja

# --- STAGE 2: Lightweight Runtime ---
FROM debian:bookworm-slim

# Install only runtime shared libraries (no compilers)
RUN apt-get update && apt-get install -y \
    libhiredis0.14 \
    libgrpc++1.51 \
    libprotobuf32 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/bin

# Copy ONLY the binary and the config
COPY --from=builder /app/Build/RateLimiter .
COPY config.toml .

# Expose gRPC port
EXPOSE 50051

CMD ["./RateLimiter"]
