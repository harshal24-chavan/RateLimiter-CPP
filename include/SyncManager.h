#pragma once

#include "SpscQueue.hpp"
#include "SyncTask.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "EndPointRegistry.h"

class SyncManager {
private:
  std::shared_ptr<sw::redis::Redis> _redis;
  std::shared_ptr<EndPointRegistry> _endPointRegistry;
  // and it would also be required for pop function in redis consumer
  std::vector<std::unique_ptr<SPSCQueue<SyncTask>>>
      queueList; // queue of each GRPC thread

  // this will be the thread index i.e what index
  // should the new thread be given
  std::atomic<size_t> index{0};

  // giving default value and will be updated in constructor
  size_t endpointCount{3};

public:
  SyncManager(size_t threadCount, size_t epc,
              std::shared_ptr<sw::redis::Redis> redis,
              std::shared_ptr<EndPointRegistry> registry)
      : _redis(std::move(redis)), _endPointRegistry(std::move(registry)) {
    // Optimization: Reserve memory so the vector doesn't
    // reallocate while we're filling it.
    endpointCount = epc;

    queueList.reserve(threadCount);

    for (size_t i = 0; i < threadCount; i++) {
      queueList.emplace_back(std::make_unique<SPSCQueue<SyncTask>>());
    }
  }

  size_t getLane() {

    size_t lane = index.fetch_add(1, std::memory_order_relaxed);

    if (lane >= queueList.size()) {
      // This means more gRPC threads started than we planned for!
      throw std::runtime_error("Exceeded pre-allocated SPSC lanes.");
    }
    return lane;
  }

  bool pushTask(size_t &ind, SyncTask &item) {
    return (queueList[ind]->push(std::move(item)));
  }

  bool popTask(size_t &ind, SyncTask &item) {
    return (queueList[ind]->pop(item));
  }

  void run() {
    // Pre-allocate space ONCE outside the loop
    std::vector<std::unordered_map<uint64_t, uint32_t>> aggregationTable(
        endpointCount);
    for (auto &map : aggregationTable)
      map.reserve(10000);

    auto lastFlush = std::chrono::steady_clock::now();

    // {endpoint, userId}
    std::vector<std::pair<std::string, uint64_t>> priorityUsers;

    while (true) { // Keep the thread alive!
      bool activity = false;

      // Visiting every queue (Producer Lane)
      for (auto &lane : queueList) {
        SyncTask task;
        while (lane->pop(task)) {
          uint32_t newLocalCount =
              (aggregationTable[task.endpoint_id][task.user_id_hash] +=
               task.increment);
          activity = true;

          // getting endpoint limit
          int endPointLimit =
              _endPointRegistry->getEndPointLimit(task.endpoint_id);

          if (newLocalCount >= endPointLimit * 0.8) {
            // if user has crossed 80% of the endPointLimit
            // then add to priorityList -> endpoint string and userID
            priorityUsers.push_back(
                {_endPointRegistry->getEndPointString(task.endpoint_id),
                 task.user_id_hash});
          }
        }
      }

      // 3. The Flush Logic (Check every ~5ms)
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush)
              .count() >= 5) {
        flushToRedis(aggregationTable);

        // CRITICAL: Clear the maps but KEEP the reserved memory
        for (auto &map : aggregationTable)
          map.clear();

        lastFlush = now;
      }

      // 4. Back-off Strategy: If no one is sending requests, rest the CPU
      if (!activity) {
        std::this_thread::yield();
      }
    }
  }

  void
  flushToRedis(std::vector<std::unordered_map<uint64_t, uint32_t>> &table) {
    // creating redis pipeline
    auto pipe = _redis->pipeline();
    bool hasData = false;

    for (size_t epId = 0; epId < table.size(); epId++) {
      if (table[epId].empty())
        continue;

      std::string epName = _endPointRegistry->getEndPointString(epId);

      for (auto const &[userHash, count] : table[epId]) {
        std::string key = "rl:" + epName + ":" + std::to_string(userHash);
        pipe.incrby(key, count);
        hasData = true;
      }
    }
    if (hasData) {
      pipe.exec();
    }
  }

  void pullGlobalStates(
      std::vector<std::pair<std::string, uint64_t>> priorityUsers) {
    std::vector<std::string> keys;
    keys.reserve(priorityUsers.size());

    // predefined points in a key
    std::string_view rl = "rl:";
    std::string_view colon = ":";

    for (const auto &[ep, userID] : priorityUsers) {

      const size_t bufferSize = 21;
      char buffer[bufferSize];

      std::to_chars_result res =
          std::to_chars(buffer, buffer + bufferSize, userID);
      std::string_view userID_string(buffer, res.ptr - buffer);

      // key creation
      size_t size = rl.size() + colon.size() + ep.size() + userID_string.size();
      std::string k;
      k.reserve(size);
      k.append(rl).append(ep).append(colon).append(userID_string);

      keys.emplace_back(k);
    }

    // pulling the global truth
    std::vector<std::optional<std::string>> res;
    _redis->mget(keys.begin(), keys.end(), std::back_inserter(res));

    for (size_t ind = 0; ind < res.size(); ind++) {
    }
  }
};
