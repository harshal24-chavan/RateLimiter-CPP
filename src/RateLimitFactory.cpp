#include "RateLimitFactory.h"
#include "FixedWindow.h"
#include "TokenBucket.h"
#include <iostream>
#include <memory>
#include <sw/redis++/redis.h>

std::unique_ptr<IRateLimitStrategy>
RateLimitFactory::createStrategy(const RuleConfig &config,
                                 std::shared_ptr<sw::redis::Redis> redis) {
  if (config.strategy_type == "fixed_window") {
    std::cout << "factory: [Endpoint: " << config.endpoint
              << "] [Limit:  " << config.limit
              << "] [Window: " << config.window_seconds << "]" << std::endl;
    return std::make_unique<FixedWindow>(config.endpoint, config.limit,
                                         config.window_seconds);
  }
  // else if (config.strategy_type == "token_bucket") {
  //   return std::make_unique<TokenBucket>(redis, config.endpoint,
  //   config.limit,
  //                                        config.window_seconds);
  // }

  // else return "TokenBucket"

  // if nothing is correct then return nullptr
  return nullptr;
}
