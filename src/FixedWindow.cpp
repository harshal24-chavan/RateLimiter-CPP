#include "FixedWindow.h"
#include <atomic>
#include <iostream>

FixedWindow::FixedWindow(const std::string &endpoint, int limit,
                         int window_seconds)
    : _endpoint(endpoint), _limit(limit), _window_seconds(window_seconds) {

  // resizing  to secure 16 shard spaces
  shards = std::make_unique<std::array<Shard, numOfShards>>();

  std::cout << "FIXED WINDOW: [Endpoint: " << endpoint << "] [Limit:  " << limit
            << "] [Window: " << window_seconds << "]" << std::endl;
}

std::atomic<int> cnt{0};

RateLimitResult FixedWindow::isAllowed(uint64_t userHash) {
  // get map shard
  size_t shardIndex = (numOfShards - 1) & userHash;
  Shard &shard = (*shards)[shardIndex];

  // locking the mutex for this particular map
  // so that other maps are readily available
  std::lock_guard<std::mutex> lck(shard.mtx);

  // Reset: If time has moved to a new window, clear this shard
  int64_t currentWindow = getCurrentWindowId();
  if (currentWindow > shard.lastWindowId) {
    shard.countMap.clear();
    shard.lastWindowId = currentWindow;
  }

  uint32_t &count = shard.countMap[userHash];
  // only allow if the count is within limit
  if (count < static_cast<uint32_t>(_limit)) {
    count++;
    return {true, _limit - static_cast<int>(count)};
  }

  return {false, 0};
}
void FixedWindow::updateGlobalCount(uint64_t userHash, uint32_t globalCount) {
  size_t idx = userHash & (numOfShards - 1);
  auto &shard = (*shards)[idx];

  std::lock_guard<std::mutex> lock(shard.mtx);

  // Only update if Redis has a "more recent" (higher) count than our local
  // cache
  if (globalCount > shard.countMap[userHash]) {
    shard.countMap[userHash] = globalCount;
  }
}
