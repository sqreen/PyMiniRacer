#ifndef INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
#define INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include "isolate_manager.h"
#include "v8-isolate.h"

namespace MiniRacer {

class CancelableTaskState;

/** A handle to potentially cancel a cancelable task. */
class CancelableTaskHandle {
 public:
  explicit CancelableTaskHandle(
      std::shared_ptr<CancelableTaskState> task_state);
  ~CancelableTaskHandle();

  CancelableTaskHandle(const CancelableTaskHandle&) = delete;
  auto operator=(const CancelableTaskHandle&) -> CancelableTaskHandle& = delete;
  CancelableTaskHandle(CancelableTaskHandle&&) = delete;
  auto operator=(CancelableTaskHandle&& other) -> CancelableTaskHandle& =
                                                      delete;

  void Cancel();

 private:
  std::shared_ptr<CancelableTaskState> task_state_;
};

/** Grafts a concept of cancelable tasks on top of an IsolateManager. */
class CancelableTaskRunner {
 public:
  explicit CancelableTaskRunner(IsolateManager* isolate_manager);

  /**
   * Schedule the given runnable.
   *
   * If the returned CancableTaskHandle::Cancel() is called before or during the
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
                OnCanceled on_canceled)
      -> std::unique_ptr<CancelableTaskHandle>;

 private:
  IsolateManager* isolate_manager_;
};

/** Keeps track of status of a cancelable task. */
class CancelableTaskState {
 public:
  explicit CancelableTaskState(IsolateManager* isolate_manager);

  void Cancel();

  auto SetRunningIfNotCanceled() -> bool;

  auto SetCompleteIfNotCanceled() -> bool;

 private:
  IsolateManager* isolate_manager_;
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
                 std::shared_ptr<CancelableTaskState> task_state);

  void Run(v8::Isolate* isolate);

 private:
  Runnable runnable_;
  OnCompleted on_completed_;
  OnCanceled on_canceled_;
  std::shared_ptr<CancelableTaskState> task_state_;
};

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline CancelableTask<Runnable, OnCompleted, OnCanceled>::CancelableTask(
    Runnable runnable,
    OnCompleted on_completed,
    OnCanceled on_canceled,
    std::shared_ptr<CancelableTaskState> task_state)
    : runnable_(std::move(runnable)),
      on_completed_(std::move(on_completed)),
      on_canceled_(std::move(on_canceled)),
      task_state_(std::move(task_state)) {}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline void CancelableTask<Runnable, OnCompleted, OnCanceled>::Run(
    v8::Isolate* isolate) {
  if (!task_state_->SetRunningIfNotCanceled()) {
    // Canceled before we started the task.
    on_canceled_();

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
    on_canceled_();
    return;
  }

  on_completed_(std::move(result));
}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline auto CancelableTaskRunner::Schedule(Runnable runnable,
                                           OnCompleted on_completed,
                                           OnCanceled on_canceled)
    -> std::unique_ptr<CancelableTaskHandle> {
  std::shared_ptr<CancelableTaskState> task_state =
      std::make_shared<CancelableTaskState>(isolate_manager_);

  CancelableTask<Runnable, OnCompleted, OnCanceled> task(
      std::move(runnable), std::move(on_completed), std::move(on_canceled),
      task_state);

  isolate_manager_->Run([task = std::move(task)](v8::Isolate* isolate) mutable {
    task.Run(isolate);
  });

  return std::make_unique<CancelableTaskHandle>(task_state);
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
