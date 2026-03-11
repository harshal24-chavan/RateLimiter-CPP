#include "../include/FixedWindow.h"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

/**
 * Benchmark Configuration
 */
const uint64_t TOTAL_REQUESTS_PER_THREAD = 10'000'000;
const int NUM_THREADS = 4; // Matching your i3-540 (2 Cores, 4 Threads)
const uint32_t LIMIT = 1'000'000;
const uint32_t WINDOW = 60;

void run_bench(FixedWindow &fw, int thread_id,
               std::atomic<uint64_t> &total_allowed) {
  uint64_t allowed_count = 0;

  // Start timing
  auto start = std::chrono::high_resolution_clock::now();

  for (uint64_t i = 0; i < TOTAL_REQUESTS_PER_THREAD; ++i) {
    // We use (i + thread_id) to ensure threads hit different shards
    // but also overlap occasionally to test lock contention.
    uint64_t user_hash = i + (thread_id * 1000);

    if (fw.isAllowed(user_hash).allowed) {
      allowed_count++;
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  total_allowed += allowed_count;

  // Report per-thread performance
  double mops = (TOTAL_REQUESTS_PER_THREAD / elapsed.count()) / 1'000'000.0;
  std::cout << "[Thread " << thread_id << "] Finished in " << std::fixed
            << std::setprecision(2) << elapsed.count() << "s "
            << "(" << mops << " Mops/s)" << std::endl;
}

int main() {
  std::cout << "Starting FixedWindow Benchmark..." << std::endl;
  std::cout << "Threads: " << NUM_THREADS
            << " | Requests/Thread: " << TOTAL_REQUESTS_PER_THREAD << std::endl;
  std::cout << "-------------------------------------------------------"
            << std::endl;

  FixedWindow fw("bench_endpoint", LIMIT, WINDOW);
  std::vector<std::thread> workers;
  std::atomic<uint64_t> total_allowed{0};

  auto total_start = std::chrono::high_resolution_clock::now();

  // Spawn workers
  for (int i = 0; i < NUM_THREADS; ++i) {
    workers.emplace_back(run_bench, std::ref(fw), i, std::ref(total_allowed));
  }

  // Join workers
  for (auto &t : workers) {
    t.join();
  }

  auto total_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> total_elapsed = total_end - total_start;

  double aggregate_mops =
      ((TOTAL_REQUESTS_PER_THREAD * NUM_THREADS) / total_elapsed.count()) /
      1'000'000.0;

  std::cout << "-------------------------------------------------------"
            << std::endl;
  std::cout << "Total Requests: " << TOTAL_REQUESTS_PER_THREAD * NUM_THREADS
            << std::endl;
  std::cout << "Total Allowed:  " << total_allowed.load() << std::endl;
  std::cout << "Aggregate Throughput: " << std::fixed << std::setprecision(2)
            << aggregate_mops << " Mops/s" << std::endl;

  return 0;
}
