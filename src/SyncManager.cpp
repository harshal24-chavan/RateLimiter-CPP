
#include "SyncManager.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sw/redis++/queued_redis.h>
#include <thread>

static std::atomic<int> global_thread_counter{0};
static thread_local int my_assigned_lane = -1;

SyncManager::SyncManager(
    size_t threadCount, size_t epc, std::shared_ptr<sw::redis::Redis> redis,
    std::shared_ptr<EndPointRegistry> registry,
    std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
        StrategyMAP)
    : _redis(std::move(redis)), _endPointRegistry(std::move(registry)),
      _strategyMap(std::move(StrategyMAP)), endpointCount(epc) {
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

// bool SyncManager::pushTask(size_t &ind, SyncTask &item) {
//   return queueList[ind]->push(std::move(item));
// }

bool SyncManager::pushTask(size_t &ind, SyncTask &item) {
  // Every thread gets an ID the first time it calls this
  if (my_assigned_lane == -1) {
    my_assigned_lane =
        global_thread_counter.fetch_add(1, std::memory_order_relaxed);
  }

  // 2. Safety check: did we spawn more threads than we have queues?
  if (my_assigned_lane >= queueList.size()) {
    return false;
  }

  return queueList[my_assigned_lane]->push(std::move(item));
}

void SyncManager::run() {
  // 1. Setup
  std::vector<std::unordered_map<uint64_t, uint32_t>> aggregationTable(
      endpointCount);
  for (auto &map : aggregationTable)
    map.reserve(10000);

  auto lastFlush = std::chrono::steady_clock::now();
  std::vector<std::pair<std::string, uint64_t>> priorityUsers;
  bool hasPendingData = false;

  std::cout << "SyncManager: Consumer thread started." << std::endl;

  // Use a single loop logic that handles both running and draining
  while (true) {
    bool isRunning = running.load(std::memory_order_relaxed);
    bool activity = false;

    // 2. Poll all lanes
    for (auto &lane : queueList) {
      SyncTask task;
      while (lane->pop(task)) {
        activity = true;
        hasPendingData = true;
        uint32_t newCount =
            (aggregationTable[task.endpoint_id][task.user_id_hash] +=
             task.increment);

        // Priority Check
        int limit = _endPointRegistry->getEndPointLimit(task.endpoint_id);
        if (newCount >= limit * 0.8) {
          priorityUsers.push_back(
              {_endPointRegistry->getEndPointString(task.endpoint_id),
               task.user_id_hash});
        }
      }
    }

    // 3. Flush Logic
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush)
            .count();

    // Flush if: 50ms passed OR we are shutting down and have data
    if (hasPendingData && (elapsed >= 50 || !isRunning)) {
      flushToRedis(aggregationTable);
      for (auto &map : aggregationTable)
        map.clear();

      if (!priorityUsers.empty()) {
        pullGlobalStates(priorityUsers);
        priorityUsers.clear();
      }

      lastFlush = now;
      hasPendingData = false;
    }

    // 4. Exit Condition: Not running AND no more data in lanes AND no pending
    // aggregation
    if (!isRunning && !activity && !hasPendingData) {
      break;
    }

    // 5. Congestion Control
    if (!activity && isRunning) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  std::cout << "SyncManager: Consumer thread shutting down cleanly."
            << std::endl;
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

  for (size_t ind = 0; ind < res.size(); ind++) {
    if (res[ind].has_value()) {
      std::string ep = priorityUsers[ind].first;
      uint64_t userID = priorityUsers[ind].second;

      // 1. Convert Redis string to integer
      uint32_t globalCount = std::stoul(*res[ind]);

      // 2. Lookup strategy
      auto it = _strategyMap->find(ep);
      if (it != _strategyMap->end()) {
        it->second.strategy->updateGlobalCount(userID, globalCount);
      }
    }
  }
}
