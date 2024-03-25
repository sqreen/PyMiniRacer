#ifndef INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
#define INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include "task_runner.h"

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

/** Grafts a concept of cancelable tasks on top of a TaskRunner. */
class CancelableTaskRunner {
 public:
  explicit CancelableTaskRunner(TaskRunner* task_runner);

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
  template <typename T>
  auto Schedule(std::function<T()> runnable,
                std::function<void(T)> on_completed,
                std::function<void()> on_canceled)
      -> std::unique_ptr<CancelableTaskHandle>;

 private:
  TaskRunner* task_runner_;
};

/** Keeps track of status of a cancelable task. */
class CancelableTaskState {
 public:
  explicit CancelableTaskState(TaskRunner* task_runner);

  void Cancel();

  auto SetRunningIfNotCanceled() -> bool;

  auto SetCompleteIfNotCanceled() -> bool;

 private:
  TaskRunner* task_runner_;
  enum State : std::uint8_t {
    kNotStarted = 0,
    kRunning = 1,
    kCompleted = 2,
    kCanceled = 3
  } state_;
  std::mutex mutex_;
};

template <typename T>
class CancelableTask {
 public:
  CancelableTask(std::function<T()> runnable,
                 std::function<void(T)> on_completed,
                 std::function<void()> on_canceled,
                 std::shared_ptr<CancelableTaskState> task_state);

  void Run();

 private:
  std::function<T()> runnable_;
  std::function<void(T)> on_completed_;
  std::function<void()> on_canceled_;
  std::shared_ptr<CancelableTaskState> task_state_;
};

template <typename T>
inline CancelableTask<T>::CancelableTask(
    std::function<T()> runnable,
    std::function<void(T)> on_completed,
    std::function<void()> on_canceled,
    std::shared_ptr<CancelableTaskState> task_state)
    : runnable_(std::move(runnable)),
      on_completed_(std::move(on_completed)),
      on_canceled_(std::move(on_canceled)),
      task_state_(std::move(task_state)) {}

template <typename T>
inline void CancelableTask<T>::Run() {
  if (!task_state_->SetRunningIfNotCanceled()) {
    // Canceled before we started the task.
    on_canceled_();

    return;
  }

  T result = runnable_();

  if (!task_state_->SetCompleteIfNotCanceled()) {
    // Canceled while running.
    // Note that we might actually complete our call to runnable_() and still
    // report the task as canceled, if a cancel request comes in right at the
    // end. Or we might have been halfway through running some JavaScript code
    // when the Cancel call came in and we called
    // task_runner_->TerminateOngoingTask, at which point the JS code would
    // never finish.
    // Keeping track of what happened, if it matters at all, is the caller's
    // responsibility. The only guarantee we provide is that we call *exactly
    // one of* on_canceled or on_completed.
    on_canceled_();
    return;
  }

  on_completed_(std::move(result));
}

template <typename T>
inline auto CancelableTaskRunner::Schedule(std::function<T()> runnable,
                                           std::function<void(T)> on_completed,
                                           std::function<void()> on_canceled)
    -> std::unique_ptr<CancelableTaskHandle> {
  std::shared_ptr<CancelableTaskState> task_state =
      std::make_shared<CancelableTaskState>(task_runner_);

  std::shared_ptr<CancelableTask<T>> task = std::make_shared<CancelableTask<T>>(
      std::move(runnable), std::move(on_completed), std::move(on_canceled),
      task_state);

  task_runner_->Run([task]() { task->Run(); });

  return std::make_unique<CancelableTaskHandle>(task_state);
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
