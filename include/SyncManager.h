#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <sw/redis++/redis.h>
#include <unordered_map>
#include <vector>

#include "EndPointRegistry.h"
#include "SpscQueue.hpp"
#include "SyncTask.h"

class SyncManager {
public:
  SyncManager(size_t threadCount, size_t epc,
              std::shared_ptr<sw::redis::Redis> redis,
              std::shared_ptr<EndPointRegistry> registry,
              std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
                  StrategyMAP);

  // Thread-safe lane assignment for gRPC worker threads
  size_t getLane();

  // Producers (gRPC Threads) call this
  bool pushTask(size_t &ind, SyncTask &item);

  // Main consumer loop
  void run();

  // stop funciton to stop the run() infinite loop
  void stop();
  void pullGlobalStates(
      std::vector<std::pair<std::string, uint64_t>> &priorityUsers);

private:
  // Internal helper functions
  void flushToRedis(std::vector<std::unordered_map<uint64_t, uint32_t>> &table);

  std::atomic<bool> running{true};
  std::shared_ptr<sw::redis::Redis> _redis;
  std::shared_ptr<EndPointRegistry> _endPointRegistry;

  // One SPSC queue per gRPC thread to eliminate contention
  std::vector<std::unique_ptr<SPSCQueue<SyncTask>>> queueList;

  std::atomic<size_t> index{0};
  size_t endpointCount;

  std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
      _strategyMap;
};
