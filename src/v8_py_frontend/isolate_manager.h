#ifndef INCLUDE_MINI_RACER_ISOLATE_MANAGER_H
#define INCLUDE_MINI_RACER_ISOLATE_MANAGER_H

#include <v8-isolate.h>
#include <v8-platform.h>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace MiniRacer {

class IsolateMessagePump;

/** Owns a v8::Isolate and mediates access to it via a task queue.
 *
 * Instances of v8::Isolate are not thread safe, and yet we need to run a
 * continuous message pump thread, and Python will call in from various
 * threads. One strategy for making an isolate thread-safe is to use a
 * v8::Locker to gate usage, but this seems hard to get right (especially
 * when the message pump thread blocks indefinitely on work and then dispatches
 * that work, apparently without bothering to grab the lock itself).
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
  void Run(Runnable runnable);

  /** Schedules a task to run on the foreground thread, using
   * v8::TaskRunner::PostTask. Awaits task completion. */
  template <typename Runnable>
  auto RunAndAwait(Runnable runnable)
      -> std::invoke_result_t<Runnable, v8::Isolate*>;

  void TerminateOngoingTask();

 private:
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
  std::shared_ptr<IsolateMessagePump> message_pump_;
  v8::Isolate* isolate_;
};

/** Runs the Isolate MessagePump in a thread. */
class IsolateMessagePump {
 public:
  explicit IsolateMessagePump(v8::Platform* platform);

  static auto Start(const std::shared_ptr<IsolateMessagePump>& message_pump)
      -> v8::Isolate*;

  void ShutDown();

 private:
  void PumpMessages();

  v8::Platform* platform_;
  bool shutdown_flag_;
  std::promise<v8::Isolate*> isolate_promise_;
  std::shared_future<v8::Isolate*> isolate_future_;
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
inline void IsolateManager::Run(Runnable runnable) {
  auto task =
      std::make_unique<AdHocTask<Runnable>>(std::move(runnable), isolate_);
  platform_->GetForegroundTaskRunner(isolate_)->PostTask(std::move(task));
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
inline auto IsolateManager::RunAndAwait(Runnable runnable)
    -> std::invoke_result_t<Runnable, v8::Isolate*> {
  std::promise<std::invoke_result_t<Runnable, v8::Isolate*>> prom;

  auto run_and_set_result = [&prom, &runnable](v8::Isolate* isolate) {
    RunAndSetPromiseValue(isolate, std::move(runnable), prom);
  };

  Run(std::move(run_and_set_result));

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
