#ifndef INCLUDE_MINI_RACER_ISOLATE_MANAGER_H
#define INCLUDE_MINI_RACER_ISOLATE_MANAGER_H

#include <v8-isolate.h>
#include <v8-platform.h>
#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include "isolate_holder.h"

namespace MiniRacer {

/** Owns a v8::Isolate and mediates access to it via a task queue.
 *
 * Instances of v8::Isolate are not thread safe, and yet we need to run an
 * infinite message pump thread, and Python will call in from various threads.
 * One strategy for making an isolate thread-safe is to use a v8::Locker, but
 * this seems hard to get right (especially when the message pump thread blocks
 * indefinitely on work and then dispatches that work, apparently without
 * bothering to grab the lock itself).
 * So instead we employ the strategy of "hiding" the isolate pointer within this
 * class, and only expose it via callbacks from the isolate's task queue.
 * Anything which wants to interact with the isolate must "get in line" by
 * scheduling a task with the IsolateManager. */
class IsolateManager {
 public:
  explicit IsolateManager(v8::Platform* platform);
  ~IsolateManager();

  IsolateManager(const IsolateManager&) = delete;
  auto operator=(const IsolateManager&) -> IsolateManager& = delete;
  IsolateManager(IsolateManager&&) = delete;
  auto operator=(IsolateManager&& other) -> IsolateManager& = delete;

  /** Schedules a task to run on the foreground thread, using
   * v8::TaskRunner::PostTask. */
  template <typename Runnable>
  void Run(Runnable runnable, bool interrupt = false);

  /** Schedules a task to run on the foreground thread, using
   * v8::TaskRunner::PostTask. Awaits task completion. */
  template <typename Runnable>
  void RunAndAwait(Runnable runnable, bool interrupt = false);

  void TerminateOngoingTask();

 private:
  void PumpMessages();

  /** Translate from a callback from v8::Isolate::RequestInterrupt into a
   * v8::Task::Run. */
  static void RunInterrupt(v8::Isolate* /*isolate*/, void* data);

  v8::Platform* platform_;
  IsolateHolder isolate_holder_;
  std::shared_ptr<v8::TaskRunner> task_runner_;
  std::atomic<bool> shutdown_;
  std::thread thread_;
};

/** Just a silly way to run code on the foreground task runner thread. */
template <typename Runnable>
class AdHocTask : public v8::Task {
 public:
  explicit AdHocTask(Runnable runnable, v8::Isolate* isolate);

  void Run() override;

 private:
  Runnable runnable_;
  v8::Isolate* isolate_;
};

template <typename Runnable>
inline void IsolateManager::Run(Runnable runnable, bool interrupt) {
  auto task = std::make_unique<AdHocTask<Runnable>>(std::move(runnable),
                                                    isolate_holder_.Get());
  if (interrupt) {
    isolate_holder_.Get()->RequestInterrupt(&IsolateManager::RunInterrupt,
                                            task.release());
  } else {
    task_runner_->PostTask(std::move(task));
  }
}

template <typename Runnable>
inline void IsolateManager::RunAndAwait(Runnable runnable, bool interrupt) {
  std::promise<void> prom;

  auto run_and_set_result = [&prom, &runnable](v8::Isolate* isolate) {
    runnable(isolate);
    prom.set_value();
  };

  Run(std::move(run_and_set_result), interrupt);

  prom.get_future().get();
}

template <typename Runnable>
inline AdHocTask<Runnable>::AdHocTask(Runnable runnable, v8::Isolate* isolate)
    : runnable_(std::move(runnable)), isolate_(isolate) {}

template <typename Runnable>
inline void AdHocTask<Runnable>::Run() {
  runnable_(isolate_);
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_MANAGER_H
