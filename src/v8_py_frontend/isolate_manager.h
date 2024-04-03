#ifndef INCLUDE_MINI_RACER_ISOLATE_MANAGER_H
#define INCLUDE_MINI_RACER_ISOLATE_MANAGER_H

#include <v8-isolate.h>
#include <v8-platform.h>
#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
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

  /** Schedules a task to run on the foreground thread, using
   * v8::TaskRunner::PostTask. */
  template <typename Runnable>
  void Run(Runnable runnable, bool interrupt = false);

  /** Schedules a task to run on the foreground thread, using
   * v8::TaskRunner::PostTask. Awaits task completion. */
  template <typename Runnable>
  auto RunAndAwait(Runnable runnable, bool interrupt = false)
      -> std::invoke_result_t<Runnable, v8::Isolate*>;

  void TerminateOngoingTask();

  void Shutdown();

 private:
  void PumpMessages();

  /** Translate from a callback from v8::Isolate::RequestInterrupt into a
   * v8::Task::Run. */
  static void RunInterrupt(v8::Isolate* /*isolate*/, void* data);

  template <typename Runnable>
  static auto RunAndSetPromiseValue(v8::Isolate* isolate,
                                    Runnable runnable,
                                    std::promise<void>& prom);

  template <typename Runnable, typename T>
  static auto RunAndSetPromiseValue(v8::Isolate* isolate,
                                    Runnable runnable,
                                    std::promise<T>& prom);

  v8::Platform* platform_;
  IsolateHolder isolate_holder_;
  std::shared_ptr<v8::TaskRunner> task_runner_;
  std::atomic<bool> shutdown_;
  std::thread thread_;
};

class IsolateManagerStopper {
 public:
  explicit IsolateManagerStopper(IsolateManager* isolate_manager);
  ~IsolateManagerStopper();

  IsolateManagerStopper(const IsolateManagerStopper&) = delete;
  auto operator=(const IsolateManagerStopper&) -> IsolateManagerStopper& =
                                                      delete;
  IsolateManagerStopper(IsolateManagerStopper&&) = delete;
  auto operator=(IsolateManagerStopper&& other) -> IsolateManagerStopper& =
                                                       delete;

 private:
  IsolateManager* isolate_manager_;
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
inline auto IsolateManager::RunAndSetPromiseValue(v8::Isolate* isolate,
                                                  Runnable runnable,
                                                  std::promise<void>& prom) {
  runnable(isolate);
  prom.set_value();
}

template <typename Runnable, typename T>
inline auto IsolateManager::RunAndSetPromiseValue(v8::Isolate* isolate,
                                                  Runnable runnable,
                                                  std::promise<T>& prom) {
  prom.set_value(runnable(isolate));
}

/** Schedules a task to run on the foreground thread, using
 * v8::TaskRunner::PostTask. Awaits task completion. */
template <typename Runnable>
inline auto IsolateManager::RunAndAwait(Runnable runnable, bool interrupt)
    -> std::invoke_result_t<Runnable, v8::Isolate*> {
  std::promise<std::invoke_result_t<Runnable, v8::Isolate*>> prom;

  auto run_and_set_result = [&prom, &runnable](v8::Isolate* isolate) {
    RunAndSetPromiseValue(isolate, runnable, prom);
  };

  Run(std::move(run_and_set_result), interrupt);

  return prom.get_future().get();
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
