#pragma once
#include <cstdint>
#include<string>

struct RateLimitResult{
  bool allowed ;
  int32_t remaining;
};

class IRateLimitStrategy{
  public:
    virtual RateLimitResult isAllowed(const std::string& identifier) = 0;
    virtual ~IRateLimitStrategy() = default;
};

