#pragma once
#include "IRateLimitStrategy.h"
#include <sw/redis++/redis++.h>
#include <memory>

class FixedWindow : public IRateLimitStrategy {
private:
    std::shared_ptr<sw::redis::Redis> _redis;
    std::string _endpoint;
    int _limit;
    int _window_seconds;
    std::string _script_sha; // For EVALSHA

public:
    FixedWindow(std::shared_ptr<sw::redis::Redis> redis, 
                const std::string& endpoint, 
                int limit, 
                int window_seconds);

    RateLimitResult isAllowed(const std::string& identifier) override;
};
