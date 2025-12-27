#pragma once

#include "RateLimitStrategy.h"
#include <string>

class FixedWindow : public IRateLimitStrategy {
public:
  bool isAllowed(const std::string &identifier) override;
};
