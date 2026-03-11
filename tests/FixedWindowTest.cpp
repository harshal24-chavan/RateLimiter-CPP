#include "../include/FixedWindow.h"
#include <cassert>
#include <iostream>
#include <thread>

void test_basic_limit() {
  // Limit: 5 requests per 2 seconds
  FixedWindow fw("test", 5, 2);
  uint64_t user = 12345;

  // First 5 should be allowed
  for (int i = 0; i < 5; ++i) {
    assert(fw.isAllowed(user).allowed == true);
  }

  // 6th should be blocked
  assert(fw.isAllowed(user).allowed == false);
  std::cout << "Basic Limit Test: PASSED\n";
}

void test_window_reset() {
  FixedWindow fw("test", 5, 1);
  uint64_t user = 12345;

  for (int i = 0; i < 5; ++i)
    fw.isAllowed(user);
  assert(fw.isAllowed(user).allowed == false);

  // Wait for window to expire
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  // Should be allowed again
  assert(fw.isAllowed(user).allowed == true);
  std::cout << "Window Reset Test: PASSED\n";
}

int main() {
  test_basic_limit();
  test_window_reset();
  return 0;
}
