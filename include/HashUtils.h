#pragma once

#include <cstdint>
#include <string_view>

class HashUtils {
private:
  // this Numbers are chosen by the creaters of FNV-1a hash algo for 64bits
  static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
  static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

public:
  static uint64_t hashID(std::string_view userID) {
    uint64_t hash = FNV_OFFSET_BASIS;
    for (char ch : userID) {
      hash = hash ^ static_cast<uint8_t>(ch);
      hash = hash * FNV_PRIME;
    }

    return hash;
  }
};
