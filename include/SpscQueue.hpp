
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdatomic.h>

static constexpr size_t RING_BUFFER_SIZE = 16384; // 2^14
static constexpr size_t MASK = RING_BUFFER_SIZE - 1;

template <typename T> class SPSCQueue {
private:
  // write_pos will be modified by publisher alone,
  // and it is read only for consumer
  alignas(64) std::atomic<uint64_t> write_pos{0};
  // this cached_read_pos is only going to be used by the producer
  uint64_t cached_read_pos{0};

  // read_pos will be modified by consumer alone,
  // and it is read only for publisher
  alignas(64) std::atomic<uint64_t> read_pos{0};
  // this cached_write_pos is onlyy going to be used by the consumer
  uint64_t cached_write_pos{0};

  // we need alignas(64) becuase it will give the variables their own cache
  // line, which is help us remove false sharing
  //
  // using std::unique_ptr because we can have a 64 byte struct as T
  // so in order to not get stackoverflow we are using a pointer
  alignas(64) std::unique_ptr<std::array<T, RING_BUFFER_SIZE>> ring_buffer;

public:
  SPSCQueue()
      : ring_buffer(std::make_unique<std::array<T, RING_BUFFER_SIZE>>()) {}

  bool isFull(uint64_t writePos, uint64_t readPos) {
    // this means the queue is full
    return (writePos - readPos) == RING_BUFFER_SIZE;
  }

  // for internal working of the spsc queue
  bool isEmpty(uint64_t writePos, uint64_t readPos) {
    return writePos == readPos;
  }

  // for others to check if empty
  bool isEmpty() {
    return write_pos.load(std::memory_order_relaxed) ==
           read_pos.load(std::memory_order_relaxed);
  }

  bool push(const T &item) {
    // relaxed because only this push function is going to update write_pos
    uint64_t writePos = write_pos.load(std::memory_order_relaxed);

    // uint64_t readPos = read_pos.load(std::memory_order_acquire);

    if (isFull(writePos, cached_read_pos)) {
      // we are in this code means that the queue appears to be full
      // we now need to check by confirming with the actual read_pos
      //
      // acquire becaues it is only read and not going to be modified by push
      cached_read_pos = read_pos.load(std::memory_order_acquire);
      if (isFull(writePos, cached_read_pos)) {
        return false;
      }
    }

    // masking similar to writePos % size of array;
    auto index = writePos & MASK;

    // dereferrencing the ring_buffer unique_ptr
    auto &buffer = *ring_buffer;
    buffer[index] = std::move(item);

    // releasing to notify our consumer that data is ready to be consumed.
    // is this correct?
    write_pos.store(writePos + 1, std::memory_order_release);

    return true;
  }

  bool pop(T &item) {
    // readPos is relaxed bcause only pop function modifies the read_pos atomic
    uint64_t readPos = read_pos.load(std::memory_order_relaxed);

    if (isEmpty(cached_write_pos, readPos)) {
      // we are here means the the queue appears to be empty
      // we now need to check by confirming with the actual write_pos;
      //
      // acquire becuase it is read only and not going to be modified by pop
      cached_write_pos = write_pos.load(std::memory_order_acquire);
      if (isEmpty(cached_write_pos, readPos)) {
        return false;
      }
    }

    auto index = readPos & MASK;

    auto &buffer = *ring_buffer;
    // the item is by shared reference and will get the popped value in it.
    item = std::move(buffer[index]);

    // releasing to notify our producer that data has been consumed.
    read_pos.store(readPos + 1, std::memory_order_release);

    return true;
  }
};
