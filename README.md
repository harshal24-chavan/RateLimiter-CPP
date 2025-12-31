
# 🚀 High-Performance C++ Rate Limiter

A distributed, low-latency Rate Limiting service built with **C++20**, **gRPC**, and **Redis**. This service implements multiple rate-limiting algorithms and is designed to handle thousands of requests per second with sub-30ms tail latency.



## ✨ Features

* **High Performance:** Benchmarked at ~7,000 RPS with a **p99 of 27ms**.
* **Dual Algorithms:** Supports both **Fixed Window** and **Token Bucket** strategies.
* **Atomic Operations:** Uses **Lua scripting** in Redis to prevent race conditions and ensure thread safety.
* **Dynamic Configuration:** Fully driven by a `config.toml` file for easy rule management.
* **Production Ready:** Multi-stage Docker builds (Debian Slim) for a lightweight footprint.
* **Extensible:** Strategy and Factory patterns make adding new algorithms (e.g., Leaky Bucket) seamless.

---

## 🛠 Tech Stack

* **Language:** C++20
* **Communication:** gRPC / Protocol Buffers
* **Database:** Redis (using `redis-plus-plus` & `hiredis`)
* **Config:** `tomlplusplus`
* **Infrastructure:** Docker & Docker Compose
* **Build System:** CMake & Ninja

---

## 📊 Performance Benchmarks

Tested using `ghz` on a local Dockerized environment:

| Metric | Result |
| :--- | :--- |
| **Total Requests** | 20,000 |
| **Concurrency** | 50 concurrent workers |
| **Requests/sec** | **6,939.21** |
| **Average Latency** | 13.94 ms |
| **p99 Latency** | **26.10 ms** |


<img width="581" height="557" alt="image" src="https://github.com/user-attachments/assets/06d42c40-4cf0-463b-bf79-05cdd2abbcce" />

---

## 🚀 Getting Started

### Prerequisites
* Docker & Docker Compose

### Running with Docker (Recommended)
1.  Clone the repository:
    ```bash
    git clone https://github.com/harshal24-chavan/rate-limiter-cpp.git
    cd RateLimiter-CPP
    ```
2.  Start the service:
    ```bash
    docker compose up --build -d
    ```
3.  The gRPC server will be listening on `localhost:50051`.

---

## ⚙️ Configuration

Rules are defined in `config.toml`. You can map specific endpoints to different strategies:

```toml
[redis]
host = "redis"
port = 6379

[server]
port = 50051

[[rules]]
endpoint = "/api/v1/login"
strategy_type = "token_bucket"
limit = 5
interval = 60 # seconds

[[rules]]
endpoint = "default"
strategy_type = "fixed_window"
limit = 100
interval = 3600
```

## 📂 Project Structure 
```bash
├── proto/ |
	└── ratelimiter.proto # gRPC Service and Message definitions 
├── include/ │ 
	├── IRateLimitStrategy.h # Interface for rate limit algorithms │ 
	├── FixedWindow.h # Fixed Window implementation │ 
	├── TokenBucket.h # Token Bucket implementation │ 
	├── RateLimitFactory.h # Factory for strategy instantiation │ 
	└── tomlParser.h # Config parsing logic 
├── src/ │ 
	├── main.cpp # gRPC Server implementation & Entry point │ 
	├── FixedWindow.cpp │ 
	├── TokenBucket.cpp │ 
	└── tomlParser.cpp 
├── config.toml # User-defined rate limit rules 
├── Dockerfile # Multi-stage production build 
├── docker-compose.yml # App + Redis stack orchestration 
└── CMakeLists.txt # Build configuration
```
# 🧪 Testing & Benchmarking

## Load Benchmark
Use `ghz` to measure throughput and tail latency (p99):

```bash
ghz --insecure \
    --proto ./proto/ratelimit.proto \
    --call ratelimiter.RateLimitService/Check \
    -d '{"user_id":"bench_user","endpoint":"/api/v1/login"}' \
    -n 20000 -c 50 \
    localhost:50051
```
## 🛡️ License

Distributed under the MIT License. See `LICENSE` for more information.
