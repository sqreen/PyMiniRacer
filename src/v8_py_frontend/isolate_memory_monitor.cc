#include "isolate_memory_monitor.h"
#include <v8-callbacks.h>
#include <v8-isolate.h>
#include <v8-statistics.h>
#include <cstddef>
#include <memory>
#include "isolate_manager.h"

namespace MiniRacer {

IsolateMemoryMonitor::IsolateMemoryMonitor(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager),
      state_(std::make_shared<IsolateMemoryMonitorState>()) {
  isolate_manager_
      ->Run([state = state_](v8::Isolate* isolate) {
        isolate->AddGCEpilogueCallback(&IsolateMemoryMonitor::StaticGCCallback,
                                       state.get());
      })
      .get();
}

void IsolateMemoryMonitor::SetHardMemoryLimit(size_t limit) {
  state_->SetHardMemoryLimit(limit);
}

void IsolateMemoryMonitor::SetSoftMemoryLimit(size_t limit) {
  state_->SetSoftMemoryLimit(limit);
}

auto IsolateMemoryMonitor::IsSoftMemoryLimitReached() const -> bool {
  return state_->IsSoftMemoryLimitReached();
}
auto IsolateMemoryMonitor::IsHardMemoryLimitReached() const -> bool {
  return state_->IsHardMemoryLimitReached();
}

void IsolateMemoryMonitor::ApplyLowMemoryNotification() {
  isolate_manager_
      ->Run([](v8::Isolate* isolate) { isolate->LowMemoryNotification(); })
      .get();
}

IsolateMemoryMonitor::~IsolateMemoryMonitor() {
  isolate_manager_
      ->Run([state = state_](v8::Isolate* isolate) {
        isolate->RemoveGCEpilogueCallback(
            &IsolateMemoryMonitor::StaticGCCallback, state.get());
      })
      .get();
}

void IsolateMemoryMonitor::StaticGCCallback(v8::Isolate* isolate,
                                            v8::GCType /*type*/,
                                            v8::GCCallbackFlags /*flags*/,
                                            void* data) {
  static_cast<IsolateMemoryMonitorState*>(data)->GCCallback(isolate);
}

IsolateMemoryMonitorState::IsolateMemoryMonitorState()
    : soft_memory_limit_(0),
      soft_memory_limit_reached_(false),
      hard_memory_limit_(0),
      hard_memory_limit_reached_(false) {}

auto IsolateMemoryMonitorState::IsSoftMemoryLimitReached() const -> bool {
  return soft_memory_limit_reached_;
}

auto IsolateMemoryMonitorState::IsHardMemoryLimitReached() const -> bool {
  return hard_memory_limit_reached_;
}

void IsolateMemoryMonitorState::GCCallback(v8::Isolate* isolate) {
  v8::HeapStatistics stats;
  isolate->GetHeapStatistics(&stats);
  const size_t used = stats.used_heap_size();

  soft_memory_limit_reached_ =
      (soft_memory_limit_ > 0) && (used > soft_memory_limit_);
  isolate->MemoryPressureNotification((soft_memory_limit_reached_)
                                          ? v8::MemoryPressureLevel::kModerate
                                          : v8::MemoryPressureLevel::kNone);
  if ((hard_memory_limit_ > 0) && used > hard_memory_limit_) {
    hard_memory_limit_reached_ = true;
    isolate->TerminateExecution();
  }
}

void IsolateMemoryMonitorState::SetHardMemoryLimit(size_t limit) {
  hard_memory_limit_ = limit;
  hard_memory_limit_reached_ = false;
}

void IsolateMemoryMonitorState::SetSoftMemoryLimit(size_t limit) {
  soft_memory_limit_ = limit;
  soft_memory_limit_reached_ = false;
}

}  // end namespace MiniRacer
