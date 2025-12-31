#include "FixedWindow.h"
#include <chrono>
#include <iterator>
#include <string>
#include <vector>

// The Lua script ensures atomicity: Increment and Expire happen together.
// KEYS[1] = identifier (the Redis key)
// ARGV[1] = window size in seconds
// ARGV[2] = max limit
const std::string FIXED_WINDOW_LUA = R"(
    local current = redis.call('INCR', KEYS[1])
    if current == 1 then
        redis.call('EXPIRE', KEYS[1], ARGV[1])
    end
    local ttl = redis.call('TTL', KEYS[1])
    return {current, ttl}
)";

FixedWindow::FixedWindow(std::shared_ptr<sw::redis::Redis> redis,
    const std::string &endpoint, int limit,
    int window_seconds)
  : _redis(redis), _endpoint(endpoint), _limit(limit),
  _window_seconds(window_seconds) {}

RateLimitResult FixedWindow::isAllowed(const std::string &identifier) {
  // 1. Construct a unique key for this user/endpoint combination
  std::string redis_key = "rl:fixed:" + _endpoint + ":" + identifier;

  try {
    std::vector<std::string> keys = {redis_key};
    std::vector<std::string> args = {std::to_string(_window_seconds)};

    std::vector<long long> result;
    _redis->eval(FIXED_WINDOW_LUA, keys.begin(), keys.end(), args.begin(), args.end(), std::back_inserter(result));


    long long count = result[0];
    long long ttl = result[1];

    RateLimitResult res;
    res.allowed = (count <= _limit);
    res.remaining = std::max(0LL, (long long)_limit - count);

    return res;

  } catch (const sw::redis::Error &e) {
    // Fallback logic: If Redis is down, we usually fail-open (allow request)
    // or log the error and deny. Here we fail-open for availability.
    return {true, 0};
  }
}
