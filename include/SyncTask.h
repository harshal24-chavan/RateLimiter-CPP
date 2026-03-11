#pragma once

#include <cstdint>
#include <type_traits>

struct SyncTask {
  uint64_t user_id_hash; // 8 bytes (UUID hashed to uint64_t)
  uint32_t endpoint_id;  // 4 bytes (Interned ID)
  uint32_t increment;    // 4 bytes (The count)
};
// Total: 16 bytes.
static_assert(sizeof(SyncTask) == 16, "SyncTask size mismatch!");
static_assert(std::is_trivially_copyable<SyncTask>::value,
              "SyncTask must be trivially copyable!");
