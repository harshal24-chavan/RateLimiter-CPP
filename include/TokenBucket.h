#pragma once

#include "IRateLimitStrategy.h"
#include <memory>
#include <string>
#include <sw/redis++/redis.h>

class TokenBucket: public IRateLimitStrategy{
  private:
    std::shared_ptr<sw::redis::Redis> _redis;
    std::string _endpoint;
    int _capacity;
    int _refill_rate;
  public:
    TokenBucket(std::shared_ptr<sw::redis::Redis> redis, 
                const std::string& endpoint, 
                int capacity, 
                int refill_rate);

    RateLimitResult isAllowed(const std::string& identifier) override;
};
