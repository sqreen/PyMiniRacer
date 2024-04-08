#ifndef INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
#define INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include "isolate_manager.h"
#include "v8-isolate.h"

namespace MiniRacer {

class CancelableTaskRegistry;

/** Grafts a concept of cancelable tasks on top of an IsolateManager. */
class CancelableTaskRunner {
 public:
  explicit CancelableTaskRunner(
      const std::shared_ptr<IsolateManager>& isolate_manager);

  /**
   * Schedule the given runnable.
   *
   * If Cancel() is called on the returned task ID before or during the
   * execution of runnable, the runnable will be interrupted or not called at
   * all. on_canceled() will be called if the runnable was canceled and
   * on_completed() will be called otherwise. (To restate: *exactly one of*
   * those two functions will be called.)
   *
   * We split up these into separate functors to discourage side-channel passing
   * of result data; the caller should follow the CancelableTaskRunner's view
   * regarding whether the task was completed or canceled.
   */
  template <typename Runnable, typename OnCompleted, typename OnCanceled>
  auto Schedule(Runnable runnable,
                OnCompleted on_completed,
                OnCanceled on_canceled) -> uint64_t;

  void Cancel(uint64_t task_id);

 private:
  std::shared_ptr<IsolateManager> isolate_manager_;
  std::shared_ptr<CancelableTaskRegistry> task_registry_;
};

class CancelableTaskState;

class CancelableTaskRegistry {
 public:
  explicit CancelableTaskRegistry(
      std::shared_ptr<IsolateManager> isolate_manager);

  auto Create(std::shared_ptr<CancelableTaskState> task_state) -> uint64_t;
  void Remove(uint64_t task_id);
  void Cancel(uint64_t task_id);

 private:
  std::shared_ptr<IsolateManager> isolate_manager_;
  std::mutex mutex_;
  uint64_t next_task_id_;
  std::unordered_map<uint64_t, std::shared_ptr<CancelableTaskState>> tasks_;
};

class CancelableTaskRegistryRemover {
 public:
  CancelableTaskRegistryRemover(
      uint64_t task_id,
      std::shared_ptr<CancelableTaskRegistry> task_registry);
  ~CancelableTaskRegistryRemover();

  CancelableTaskRegistryRemover(const CancelableTaskRegistryRemover&) = delete;
  auto operator=(const CancelableTaskRegistryRemover&)
      -> CancelableTaskRegistryRemover& = delete;
  CancelableTaskRegistryRemover(CancelableTaskRegistryRemover&&) = delete;
  auto operator=(CancelableTaskRegistryRemover&& other)
      -> CancelableTaskRegistryRemover& = delete;

 private:
  uint64_t task_id_;
  std::shared_ptr<CancelableTaskRegistry> task_registry_;
};

/** Keeps track of status of a cancelable task. */
class CancelableTaskState {
 public:
  explicit CancelableTaskState();

  void Cancel(IsolateManager* isolate_manager);

  auto SetRunningIfNotCanceled() -> bool;

  auto SetCompleteIfNotCanceled() -> bool;

 private:
  enum State : std::uint8_t {
    kNotStarted = 0,
    kRunning = 1,
    kCompleted = 2,
    kCanceled = 3
  } state_;
  std::mutex mutex_;
};

template <typename Runnable, typename OnCompleted, typename OnCanceled>
class CancelableTask {
 public:
  CancelableTask(Runnable runnable,
                 OnCompleted on_completed,
                 OnCanceled on_canceled,
                 const std::shared_ptr<CancelableTaskRegistry>& task_registry);

  void Run(v8::Isolate* isolate);

  auto TaskId() -> uint64_t;

 private:
  Runnable runnable_;
  OnCompleted on_completed_;
  OnCanceled on_canceled_;
  std::shared_ptr<CancelableTaskState> task_state_;
  uint64_t task_id_;
  CancelableTaskRegistryRemover task_registry_remover_;
};

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline CancelableTask<Runnable, OnCompleted, OnCanceled>::CancelableTask(
    Runnable runnable,
    OnCompleted on_completed,
    OnCanceled on_canceled,
    const std::shared_ptr<CancelableTaskRegistry>& task_registry)
    : runnable_(std::move(runnable)),
      on_completed_(std::move(on_completed)),
      on_canceled_(std::move(on_canceled)),
      task_state_(std::make_shared<CancelableTaskState>()),
      task_id_(task_registry->Create(task_state_)),
      task_registry_remover_(task_id_, task_registry) {}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline auto CancelableTask<Runnable, OnCompleted, OnCanceled>::TaskId()
    -> uint64_t {
  return task_id_;
}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline void CancelableTask<Runnable, OnCompleted, OnCanceled>::Run(
    v8::Isolate* isolate) {
  if (!task_state_->SetRunningIfNotCanceled()) {
    // Canceled before we started the task.
    on_canceled_({});

    return;
  }

  auto result = runnable_(isolate);

  if (!task_state_->SetCompleteIfNotCanceled()) {
    // Canceled while running.
    // Note that we might actually complete our call to runnable_() and still
    // report the task as canceled, if a cancel request comes in right at the
    // end. Or we might have been halfway through running some JavaScript code
    // when the Cancel call came in and we called
    // isolate_manager_->TerminateOngoingTask, at which point the JS code would
    // never finish.
    // Keeping track of what happened, if it matters at all, is the caller's
    // responsibility. The only guarantee we provide is that we call *exactly
    // one of* on_canceled or on_completed.
    // Note that we pass the result to the on_canceled_ function so it can
    // clean up the result if needed.
    on_canceled_(result);
    return;
  }

  on_completed_(std::move(result));
}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline auto CancelableTaskRunner::Schedule(Runnable runnable,
                                           OnCompleted on_completed,
                                           OnCanceled on_canceled) -> uint64_t {
  auto task =
      std::make_unique<CancelableTask<Runnable, OnCompleted, OnCanceled>>(
          std::move(runnable), std::move(on_completed), std::move(on_canceled),
          task_registry_);

  uint64_t task_id = task->TaskId();

  // clang-tidy-18 gives a strange warning about a memory leak here, which would
  // seem to me to be impossible since we're only using a unique_ptr. (And
  // testing demonstrates we don't leak here.)
  // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
  isolate_manager_->Run([task = std::move(task)](v8::Isolate* isolate) mutable {
    task->Run(isolate);
  });
  // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

  return task_id;
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
