#pragma once
#include <cstdint>
#include<string>

struct RateLimitResult{
  bool allowed ;
  int32_t remaining;
};

class IRateLimitStrategy{
  public:
    virtual bool isAllowed(const std::string& identifier) = 0;
    virtual ~IRateLimitStrategy() = default;
};

