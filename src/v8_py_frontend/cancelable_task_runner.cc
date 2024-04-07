#include "cancelable_task_runner.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
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

CancelableTaskRunner::CancelableTaskRunner(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager),
      task_registry_(
          std::make_shared<CancelableTaskRegistry>(isolate_manager)) {}

void CancelableTaskRunner::Cancel(uint64_t task_id) {
  task_registry_->Cancel(task_id);
}

CancelableTaskRegistry::CancelableTaskRegistry(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager), next_task_id_(1) {}

auto CancelableTaskRegistry::Create(
    std::shared_ptr<CancelableTaskState> task_state) -> uint64_t {
  const std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t task_id = next_task_id_++;
  tasks_[task_id] = std::move(task_state);
  return task_id;
}

void CancelableTaskRegistry::Remove(uint64_t task_id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  tasks_.erase(task_id);
}

void CancelableTaskRegistry::Cancel(uint64_t task_id) {
  std::shared_ptr<CancelableTaskState> task_state;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto iter = tasks_.find(task_id);
    if (iter == tasks_.end()) {
      return;
    }
    task_state = iter->second;
  }
  task_state->Cancel(isolate_manager_);
}

CancelableTaskRegistryRemover::CancelableTaskRegistryRemover(
    uint64_t task_id,
    std::shared_ptr<CancelableTaskRegistry> task_registry)
    : task_id_(task_id), task_registry_(std::move(task_registry)) {}

CancelableTaskRegistryRemover::~CancelableTaskRegistryRemover() {
  task_registry_->Remove(task_id_);
}

}  // end namespace MiniRacer
