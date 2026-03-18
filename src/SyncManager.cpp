
#include "SyncManager.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sw/redis++/queued_redis.h>
#include <thread>

SyncManager::SyncManager(size_t threadCount, size_t epc,
                         std::shared_ptr<sw::redis::Redis> redis,
                         std::shared_ptr<EndPointRegistry> registry)
    : _redis(std::move(redis)), _endPointRegistry(std::move(registry)),
      endpointCount(epc) {

  queueList.reserve(threadCount);
  for (size_t i = 0; i < threadCount; i++) {
    queueList.emplace_back(std::make_unique<SPSCQueue<SyncTask>>());
  }
}

size_t SyncManager::getLane() {
  size_t lane = index.fetch_add(1, std::memory_order_relaxed);
  if (lane >= queueList.size()) {
    throw std::runtime_error("Exceeded pre-allocated SPSC lanes.");
  }
  return lane;
}

bool SyncManager::pushTask(size_t &ind, SyncTask &item) {
  return queueList[ind]->push(std::move(item));
}

void SyncManager::run() {

  auto queuesHaveData = [this]() {
    for (auto &lane : queueList) {
      if (!lane->isEmpty())
        return true;
    }
    return false;
  };

  // 1. Pre-allocate aggregation tables to avoid runtime allocations
  std::vector<std::unordered_map<uint64_t, uint32_t>> aggregationTable(
      endpointCount);
  for (auto &map : aggregationTable) {
    map.reserve(10000);
  }

  auto lastFlush = std::chrono::steady_clock::now();
  std::vector<std::pair<std::string, uint64_t>> priorityUsers;

  std::cout << "SyncManager: Consumer thread started." << std::endl;

  while (running.load(std::memory_order_relaxed) || queuesHaveData()) {
    bool activity = false;

    // 2. Poll all lanes for tasks
    for (auto &lane : queueList) {
      SyncTask task;
      while (lane->pop(task)) {
        activity = true;
        uint32_t newLocalCount =
            (aggregationTable[task.endpoint_id][task.user_id_hash] +=
             task.increment);

        // Check if we need to pull global state for high-volume users
        int endPointLimit =
            _endPointRegistry->getEndPointLimit(task.endpoint_id);
        if (newLocalCount >= endPointLimit * 0.8) {
          priorityUsers.push_back(
              {_endPointRegistry->getEndPointString(task.endpoint_id),
               task.user_id_hash});
        }
      }
    }

    // 3. Periodic Flush to Redis (every 5ms)
    auto now = std::chrono::steady_clock::now();
    bool shouldFlush =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush)
            .count() >= 5;

    if (shouldFlush || (!running.load() && activity)) {
      if (activity) {
        flushToRedis(aggregationTable);

        // Clear maps but keep the capacity reserved
        for (auto &map : aggregationTable)
          map.clear();

        // Optional: Process priority users if needed
        if (!priorityUsers.empty()) {
          pullGlobalStates(priorityUsers);
          priorityUsers.clear();
        }
      }
      lastFlush = now;
    }

    // 4. Congestion control: Yield if no work was found
    if (!activity) {
      std::this_thread::yield();
    }
  }
  std::cout << "SyncManager: Consumer thread shutting down." << std::endl;
}

void SyncManager::stop() {
  // Signal the run() loop that it should start wrapping up
  running.store(false, std::memory_order_release);
}

void SyncManager::flushToRedis(
    std::vector<std::unordered_map<uint64_t, uint32_t>> &table) {
  auto pipe = _redis->pipeline();
  bool hasData = false;

  for (size_t epId = 0; epId < table.size(); epId++) {
    if (table[epId].empty())
      continue;

    std::string epName = _endPointRegistry->getEndPointString(epId);
    for (auto const &[userHash, count] : table[epId]) {
      // Key format: rl:login:12345
      std::string key = "rl:" + epName + ":" + std::to_string(userHash);
      pipe.incrby(key, count);
      hasData = true;
    }
  }

  if (hasData) {
    pipe.exec();
  }
}

void SyncManager::pullGlobalStates(
    std::vector<std::pair<std::string, uint64_t>> &priorityUsers) {
  if (priorityUsers.empty())
    return;

  std::vector<std::string> keys;
  keys.reserve(priorityUsers.size());

  for (const auto &[ep, userID] : priorityUsers) {
    char buffer[21];
    auto res = std::to_chars(buffer, buffer + sizeof(buffer), userID);
    std::string_view userID_sv(buffer, res.ptr - buffer);

    std::string k;
    k.reserve(3 + ep.size() + 1 + userID_sv.size());
    k.append("rl:").append(ep).append(":").append(userID_sv);
    keys.emplace_back(std::move(k));
  }

  std::vector<std::optional<std::string>> res;
  _redis->mget(keys.begin(), keys.end(), std::back_inserter(res));
}
