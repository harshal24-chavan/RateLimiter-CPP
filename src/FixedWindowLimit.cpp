#include "FixedWindowLimit.h"
#include <chrono>

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
                         const std::string& endpoint, 
                         int limit, 
                         int window_seconds)
    : _redis(redis), _endpoint(endpoint), _limit(limit), _window_seconds(window_seconds) {
    
    // Pre-load the script into Redis once to get the SHA1 hash for faster execution
    _script_sha = _redis->script_load(FIXED_WINDOW_LUA);
}

RateLimitResult FixedWindow::isAllowed(const std::string& identifier) {
    // 1. Construct a unique key for this user/endpoint combination
    std::string redis_key = "rl:fixed:" + _endpoint + ":" + identifier;

    try {
        // 2. Execute the pre-loaded Lua script
        // evalsha(sha, keys_list, args_list)
        auto result = _redis->evalsha(_script_sha, 
                                      {redis_key}, 
                                      {std::to_string(_window_seconds), std::to_string(_limit)});

        // 3. Parse the Redis response (Lua returns an array [count, ttl])
        long long count = result.template get<long long>(0);
        long long ttl = result.template get<long long>(1);

        RateLimitResult res;
        res.allowed = (count <= _limit);
        res.remaining = std::max(0LL, (long long)_limit - count);
        
        return res;

    } catch (const sw::redis::Error &e) {
        // Fallback logic: If Redis is down, we usually fail-open (allow request)
        // or log the error and deny. Here we fail-open for availability.
        return {true, 0, 0};
    }
}
