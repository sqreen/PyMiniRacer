#include "count_down_latch.h"

#include <condition_variable>
#include <mutex>

namespace MiniRacer {

void CountDownLatch::Increment() {
  const std::lock_guard<std::mutex> lock(mutex_);
  count_++;
}

void CountDownLatch::Decrement() {
  const std::lock_guard<std::mutex> lock(mutex_);
  count_--;

  if (count_ == 0) {
    cv_.notify_all();
  }
}

void CountDownLatch::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);

  cv_.wait(lock, [this] { return count_ == 0; });
}

CountDownLatchWaiter::CountDownLatchWaiter(CountDownLatch* latch)
    : latch_(latch) {}

CountDownLatchWaiter::~CountDownLatchWaiter() {
  latch_->Wait();
}

}  // end namespace MiniRacer
