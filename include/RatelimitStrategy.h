#pragma once
#include<string>

class IRateLimitStrategy{
  public:
    virtual bool isAllowed(const std::string& identifier);
    ~IRateLimitStrategy() = default;
};

