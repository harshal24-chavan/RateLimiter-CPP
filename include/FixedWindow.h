#pragma once
#include "IRateLimitStrategy.h"
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

struct Shard {
  // alignas(64) ensures each shard starts on a new cache line
  // to prevent False Sharing
  alignas(64) std::mutex mtx;
  std::unordered_map<uint64_t, uint32_t> countMap;
  int64_t lastWindowId = 0;
};

class FixedWindow : public IRateLimitStrategy {
private:
  std::string _endpoint;
  int _limit;
  int _window_seconds;

  static constexpr size_t numOfShards{16};

  // [shards : [userId, increment] ]
  std::unique_ptr<std::array<Shard, numOfShards>> shards;

  // Helper to calculate which "time bucket" we are in
  int64_t getCurrentWindowId() const {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count() /
           _window_seconds;
  }

public:
  FixedWindow(const std::string &endpoint, int limit, int window_seconds);

  RateLimitResult isAllowed(uint64_t userHash) override;
  void updateGlobalCount(uint64_t userHash, uint32_t globalCount) override;
};
