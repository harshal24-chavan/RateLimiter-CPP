#include "TokenBucket.h"
#include <chrono>
#include <iterator>
#include <memory>
#include <string>
#include <sw/redis++/errors.h>
#include <sw/redis++/redis.h>
#include <utility>
#include <vector>

const std::string TOKEN_BUCKET_LUA= R"(
local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local refill_rate = tonumber(ARGV[2])
local now = tonumber(ARGV[3])
local requested = 1

local bucket = redis.call('HMGET', key, 'tokens', 'last_updated')
local last_tokens = tonumber(bucket[1]) or capacity
local last_updated = tonumber(bucket[2]) or now

-- Calculate how many tokens were generated since last hit
local delta = math.max(0, now - last_updated)
local new_tokens = math.min(capacity, last_tokens + (delta * refill_rate))

local allowed = false
if new_tokens >= requested then
    new_tokens = new_tokens - requested
    allowed = true
end

-- Store the new state
redis.call('HMSET', key, 'tokens', new_tokens, 'last_updated', now)
-- Set an expiry so we don't leak memory (e.g., 1 hour of inactivity)
redis.call('EXPIRE', key, 3600)

return {allowed and 1 or 0, math.floor(new_tokens)}
)";

TokenBucket::TokenBucket(std::shared_ptr<sw::redis::Redis> redis, const std::string& endpoint, int capacity, int refill_rate)
  : _redis(redis), _endpoint(endpoint), _capacity(capacity), _refill_rate(refill_rate) {}


RateLimitResult TokenBucket::isAllowed(const std::string& identifier){
  std::string redis_key = "rl:token:" + _endpoint + ":" + identifier;

  try {

    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    std::vector<std::string> keys = {redis_key};
    std::vector<std::string> args = {
      std::to_string(_capacity),
      std::to_string(_refill_rate),
      std::to_string(seconds)
    };

    std::vector<long long> result;
    _redis->eval(TOKEN_BUCKET_LUA, keys.begin(), keys.end(), args.begin(), args.end(), std::back_inserter(result));

    long long allowed = result[0];
    long long tokens_count = result[1];

    RateLimitResult res;
    res.allowed = allowed == 1;
    res.remaining = std::max(0LL, tokens_count);

    return res;
  }
  catch(const sw::redis::Error &e){
    return {true, 0};
  }
}
