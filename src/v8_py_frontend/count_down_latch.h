#ifndef INCLUDE_MINI_RACER_COUNT_DOWN_LATCH_H
#define INCLUDE_MINI_RACER_COUNT_DOWN_LATCH_H

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace MiniRacer {

class CountDownLatch {
 public:
  void Increment();
  void Decrement();
  void Wait();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int64_t count_{0};
};

/** Calls latch->Wait() in its destructor. */
class CountDownLatchWaiter {
 public:
  explicit CountDownLatchWaiter(CountDownLatch* latch);
  ~CountDownLatchWaiter();

  CountDownLatchWaiter(const CountDownLatchWaiter&) = delete;
  auto operator=(const CountDownLatchWaiter&) -> CountDownLatchWaiter& = delete;
  CountDownLatchWaiter(CountDownLatchWaiter&&) = delete;
  auto operator=(CountDownLatchWaiter&& other) -> CountDownLatchWaiter& =
                                                      delete;

 private:
  CountDownLatch* latch_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_COUNT_DOWN_LATCH_H
