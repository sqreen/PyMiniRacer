#include "cancelable_task_runner.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include "id_maker.h"
#include "isolate_manager.h"

namespace MiniRacer {

CancelableTaskState::CancelableTaskState() : state_(State::kNotStarted) {}

void CancelableTaskState::Cancel(IsolateManager* isolate_manager) {
  const std::lock_guard<std::mutex> lock(mutex_);

  if (state_ == State::kCanceled || state_ == State::kCompleted) {
    return;
  }

  if (state_ == State::kRunning) {
    isolate_manager->TerminateOngoingTask();
  }

  state_ = State::kCanceled;
}

auto CancelableTaskState::SetRunningIfNotCanceled() -> bool {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::kCanceled) {
    return false;
  }

  state_ = State::kRunning;
  return true;
}

auto CancelableTaskState::SetCompleteIfNotCanceled() -> bool {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::kCanceled) {
    return false;
  }

  state_ = State::kCompleted;
  return true;
}

CancelableTaskRunner::CancelableTaskRunner(
    std::shared_ptr<IsolateManager> isolate_manager)
    : isolate_manager_(std::move(isolate_manager)),
      task_id_maker_(std::make_shared<IdMaker<CancelableTaskState>>()) {}

void CancelableTaskRunner::Cancel(uint64_t task_id) {
  const std::shared_ptr<CancelableTaskState> task_state =
      task_id_maker_->GetObject(task_id);
  if (!task_state) {
    // No such task found. This will commonly happen if a task is canceled
    // after it has already completed.
    return;
  }
  task_state->Cancel(isolate_manager_.get());
}

}  // end namespace MiniRacer
