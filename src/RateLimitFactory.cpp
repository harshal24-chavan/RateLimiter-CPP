#include "RateLimitFactory.h"
#include <sw/redis++/redis.h>
#include <memory>
#include "FixedWindow.h"
#include "TokenBucket.h"

std::unique_ptr<IRateLimitStrategy> RateLimitFactory::createStrategy(const RuleConfig& config, std::shared_ptr<sw::redis::Redis> redis){
  if(config.strategy_type == "fixed_window"){
    return std::make_unique<FixedWindow>(
        redis, 
        config.endpoint,
        config.limit,
        config.window_seconds
        );
  }
  else if (config.strategy_type == "token_bucket") {
    return std::make_unique<TokenBucket>(redis, config.endpoint, config.limit, config.window_seconds);
  }

  // else return "TokenBucket"


  // if nothing is correct then return nullptr
  return nullptr;
}


