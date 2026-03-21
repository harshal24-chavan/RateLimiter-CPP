#include "SyncManager.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <sw/redis++/redis.h>

#include "../include/EndPointRegistry.h"

void test_pull_global_logic() {
  std::vector<RuleConfig> rules;
  RuleConfig rc = {"test_login", "FixedWindow", 100, 60};
  rules.push_back(rc);

  auto redis = std::make_shared<sw::redis::Redis>("tcp://127.0.0.1:6379");
  auto registry = std::make_shared<EndPointRegistry>(rules);

  // have to update this
  SyncManager sync(1, 1, redis, registry, sm);

  // 2. Seed Redis: Manually set a count for a specific user
  uint64_t testUserHash = 99999;
  std::string redisKey = "rl:test_login:99999";
  redis->set(redisKey, "85"); // User is at 85% of limit

  // 3. Execute: Call the pull function
  // We simulate the priority list that would normally come from the 'run' loop
  std::vector<std::pair<std::string, uint64_t>> priorityList = {
      {"test_login", testUserHash}};

  std::cout << "Testing pullGlobalStates..." << std::endl;
  sync.pullGlobalStates(priorityList);

  // 4. Verification
  // If you added the [DEBUG] prints above, you should see "Value in Redis: 85"
  std::cout << "Test completed. Check console output for 'Value in Redis: 85'."
            << std::endl;
}

int main() {
  // test_basic_limit();
  // //   test_window_reset();
  // //
  // //   test_fixed_window_limit();
  //
  try {
    test_pull_global_logic();
  } catch (const std::exception &e) {
    std::cerr << "Test failed with error: " << e.what() << std::endl;
  }
  return 0;
}
