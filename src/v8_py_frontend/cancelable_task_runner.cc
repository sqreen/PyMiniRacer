#include "cancelable_task_runner.h"

#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>
#include "id_maker.h"
#include "isolate_manager.h"

namespace MiniRacer {

void CancelableTaskBase::SetFuture(std::future<void> fut) {
  future_promise_.set_value(std::move(fut));
}

void CancelableTaskBase::Await() {
  future_promise_.get_future().get();
}

CancelableTaskManager::CancelableTaskManager(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager),
      task_id_maker_(std::make_shared<IdMaker<CancelableTaskBase>>()) {}

CancelableTaskManager::~CancelableTaskManager() {
  // Normally, completed or canceled tasks will clean themselves out of the
  // IdMaker. However, some tasks may still be pending upon teardown. Let's
  // explicitly cancel and await any stragglers:
  const std::vector<std::shared_ptr<CancelableTaskBase>> pending_tasks =
      task_id_maker_->GetObjects();

  for (const auto& task : pending_tasks) {
    task->Cancel(isolate_manager_);
  }

  for (const auto& task : pending_tasks) {
    task->Await();
  }
}

void CancelableTaskManager::Cancel(uint64_t task_id) {
  const std::shared_ptr<CancelableTaskBase> task =
      task_id_maker_->GetObject(task_id);
  if (!task) {
    // No such task found. This will commonly happen if a task is canceled
    // after it has already completed.
    return;
  }
  task->Cancel(isolate_manager_);
}

}  // end namespace MiniRacer
