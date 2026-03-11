#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

struct RateLimitResult {
  bool allowed;
  int32_t remaining;
};

class IRateLimitStrategy {
public:
  virtual RateLimitResult isAllowed(uint64_t userHash) = 0;
  virtual void updateGlobalCount(uint64_t userHash, uint32_t globalCount) = 0;

  virtual ~IRateLimitStrategy() = default;
};
