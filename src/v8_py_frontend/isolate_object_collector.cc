#include "isolate_object_collector.h"
#include <v8-isolate.h>
#include <functional>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>
#include "isolate_manager.h"

namespace MiniRacer {

IsolateObjectCollector::IsolateObjectCollector(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager), is_collecting_(false) {}

IsolateObjectCollector::~IsolateObjectCollector() {
  std::unique_lock<std::mutex> lock(mutex_);
  collection_done_cv_.wait(lock, [this] { return !is_collecting_; });
}

void IsolateObjectCollector::StartCollectingLocked() {
  is_collecting_ = true;

  std::ignore = isolate_manager_->Run([this](v8::Isolate*) { DoCollection(); });
}

void IsolateObjectCollector::DoCollection() {
  std::vector<std::function<void()>> batch;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    batch = std::exchange(garbage_, {});
  }

  for (const auto& collector : batch) {
    collector();
  }

  const std::lock_guard<std::mutex> lock(mutex_);
  if (garbage_.empty()) {
    is_collecting_ = false;
    collection_done_cv_.notify_all();
    return;
  }

  StartCollectingLocked();
}

IsolateObjectDeleter::IsolateObjectDeleter()
    : isolate_object_collector_(nullptr) {}

IsolateObjectDeleter::IsolateObjectDeleter(
    IsolateObjectCollector* isolate_object_collector)
    : isolate_object_collector_(isolate_object_collector) {}

}  // end namespace MiniRacer
