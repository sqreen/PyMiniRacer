#ifndef INCLUDE_MINI_RACER_BREAKER_THREAD_H
#define INCLUDE_MINI_RACER_BREAKER_THREAD_H

#include <v8.h>
#include <chrono>
#include <mutex>
#include <thread>

namespace MiniRacer {

/** Spawns a separate thread which calls v8::Isolate::TerminateExecution() after
 * a timeout, if not first disengaged. */
class BreakerThread {
 public:
  BreakerThread(v8::Isolate* isolate, uint64_t timeout)
      : isolate_(isolate), timeout(timeout) {
    if (timeout > 0) {
      engaged = true;
      mutex.lock();
      thread_ = std::thread(&BreakerThread::threadMain, this);
    } else {
      engaged = false;
    }
  }

  ~BreakerThread() { disengage(); }
  BreakerThread(const BreakerThread&) = delete;
  auto operator=(const BreakerThread&) -> BreakerThread& = delete;
  BreakerThread(BreakerThread&&) = delete;
  auto operator=(BreakerThread&& other) -> BreakerThread& = delete;

  [[nodiscard]] auto timed_out() const -> bool { return timed_out_; }

  void disengage() {
    if (engaged) {
      mutex.unlock();
      thread_.join();
      engaged = false;
    }
  }

 private:
  void threadMain() {
    if (!mutex.try_lock_for(std::chrono::milliseconds(timeout))) {
      timed_out_ = true;
      isolate_->TerminateExecution();
    }
  }

  v8::Isolate* isolate_;
  uint64_t timeout;
  bool engaged;
  bool timed_out_{false};
  std::thread thread_;
  std::timed_mutex mutex;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_BREAKER_THREAD_H
