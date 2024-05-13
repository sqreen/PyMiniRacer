#include "isolate_object_collector.h"
#include <mutex>
#include "isolate_manager.h"

namespace MiniRacer {

IsolateObjectCollector::IsolateObjectCollector(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager) {}

void IsolateObjectCollector::Dispose() {
  const std::lock_guard<std::mutex> lock(mutex_);
  for (auto& func : garbage_) {
    func();
  }
  garbage_.clear();
}

IsolateObjectDeleter::IsolateObjectDeleter()
    : isolate_object_collector_(nullptr) {}

IsolateObjectDeleter::IsolateObjectDeleter(
    IsolateObjectCollector* isolate_object_collector)
    : isolate_object_collector_(isolate_object_collector) {}

}  // end namespace MiniRacer
