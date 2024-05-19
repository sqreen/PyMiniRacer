#ifndef INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
#define INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H

#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include "id_maker.h"
#include "isolate_manager.h"
#include "v8-isolate.h"

namespace MiniRacer {

class CancelableTaskBase {
 public:
  CancelableTaskBase() = default;
  virtual ~CancelableTaskBase() = default;

  CancelableTaskBase(const CancelableTaskBase&) = delete;
  auto operator=(const CancelableTaskBase&) -> CancelableTaskBase& = delete;
  CancelableTaskBase(CancelableTaskBase&&) = delete;
  auto operator=(CancelableTaskBase&& other) -> CancelableTaskBase& = delete;

  virtual void Cancel(IsolateManager* isolate_manager) = 0;

  void SetFuture(std::future<void> fut);
  void Await();

 private:
  // A promise which yields a future which blocks until the underlying task
  // is complete.
  std::promise<std::future<void>> future_promise_;
};

/** Grafts a concept of cancelable tasks on top of an IsolateManager. */
class CancelableTaskManager {
 public:
  explicit CancelableTaskManager(IsolateManager* isolate_manager);
  ~CancelableTaskManager();

  CancelableTaskManager(const CancelableTaskManager&) = delete;
  auto operator=(const CancelableTaskManager&) -> CancelableTaskManager& =
                                                      delete;
  CancelableTaskManager(CancelableTaskManager&&) = delete;
  auto operator=(CancelableTaskManager&& other) -> CancelableTaskManager& =
                                                       delete;

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
   * of result data; the caller should follow the CancelableTaskManager's view
   * regarding whether the task was completed or canceled.
   */
  template <typename Runnable, typename OnCompleted, typename OnCanceled>
  auto Schedule(Runnable runnable,
                OnCompleted on_completed,
                OnCanceled on_canceled) -> uint64_t;

  void Cancel(uint64_t task_id);

 private:
  IsolateManager* isolate_manager_;
  std::shared_ptr<IdMaker<CancelableTaskBase>> task_id_maker_;
};

template <typename Runnable, typename OnCompleted, typename OnCanceled>
class CancelableTask : public CancelableTaskBase {
 public:
  explicit CancelableTask(Runnable runnable,
                          OnCompleted on_completed,
                          OnCanceled on_canceled);

  void Run(v8::Isolate* isolate);
  void Cancel(IsolateManager* isolate_manager) override;

 private:
  Runnable runnable_;
  OnCompleted on_completed_;
  OnCanceled on_canceled_;

  std::mutex mutex_;
  enum State : std::uint8_t {
    kNotStarted = 0,
    kRunning = 1,
    kCompleted = 2,
    kCanceled = 3
  } state_;
};

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline auto CancelableTaskManager::Schedule(Runnable runnable,
                                            OnCompleted on_completed,
                                            OnCanceled on_canceled)
    -> uint64_t {
  auto task =
      std::make_shared<CancelableTask<Runnable, OnCompleted, OnCanceled>>(
          std::move(runnable), std::move(on_completed), std::move(on_canceled));

  IdHolder<CancelableTaskBase> task_id_holder(task, task_id_maker_);

  const uint64_t task_id = task_id_holder.GetId();

  std::future<void> fut = isolate_manager_->Run(
      [holder = std::move(task_id_holder), task](v8::Isolate* isolate) mutable {
        task->Run(isolate);
      });

  task->SetFuture(std::move(fut));

  return task_id;
}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline CancelableTask<Runnable, OnCompleted, OnCanceled>::CancelableTask(
    Runnable runnable,
    OnCompleted on_completed,
    OnCanceled on_canceled)
    : runnable_(std::move(runnable)),
      on_completed_(std::move(on_completed)),
      on_canceled_(std::move(on_canceled)),
      state_(State::kNotStarted) {}

template <typename Runnable, typename OnCompleted, typename OnCanceled>
inline void CancelableTask<Runnable, OnCompleted, OnCanceled>::Run(
    v8::Isolate* isolate) {
  bool was_canceled_before_run = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == State::kCanceled) {
      was_canceled_before_run = true;
    } else {
      state_ = State::kRunning;
    }
  }

  if (was_canceled_before_run) {
    on_canceled_({});
    return;
  }

  auto result = runnable_(isolate);

  bool was_canceled_during_run = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == State::kCanceled) {
      was_canceled_during_run = true;
    } else {
      state_ = State::kCompleted;
    }
  }

  if (was_canceled_during_run) {
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
inline void CancelableTask<Runnable, OnCompleted, OnCanceled>::Cancel(
    IsolateManager* isolate_manager) {
  const std::lock_guard<std::mutex> lock(mutex_);

  if (state_ == State::kCanceled || state_ == State::kCompleted) {
    return;
  }

  if (state_ == State::kRunning) {
    isolate_manager->TerminateOngoingTask();
  }

  state_ = State::kCanceled;
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CANCELABLE_TASK_RUNNER_H
