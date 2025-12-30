#pragma once
#include "IRateLimitStrategy.h"
#include "tomlParser.h"
#include <memory>
#include <string>
#include <sw/redis++/redis++.h>

class RateLimitFactory {
public:
  static std::unique_ptr<IRateLimitStrategy>
  createStrategy(const RuleConfig &config,
                 std::shared_ptr<sw::redis::Redis> redis);
};
