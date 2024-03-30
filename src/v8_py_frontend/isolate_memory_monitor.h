#ifndef INCLUDE_MINI_RACER_ISOLATE_MEMORY_MONITOR_H
#define INCLUDE_MINI_RACER_ISOLATE_MEMORY_MONITOR_H

#include <v8-callbacks.h>
#include <v8-isolate.h>
#include <cstddef>
#include "isolate_manager.h"

namespace MiniRacer {

class IsolateMemoryMonitor {
 public:
  explicit IsolateMemoryMonitor(IsolateManager* isolate_manager);
  ~IsolateMemoryMonitor();

  IsolateMemoryMonitor(const IsolateMemoryMonitor&) = delete;
  auto operator=(const IsolateMemoryMonitor&) -> IsolateMemoryMonitor& = delete;
  IsolateMemoryMonitor(IsolateMemoryMonitor&&) = delete;
  auto operator=(IsolateMemoryMonitor&& other) -> IsolateMemoryMonitor& =
                                                      delete;

  [[nodiscard]] auto IsSoftMemoryLimitReached() const -> bool;
  [[nodiscard]] auto IsHardMemoryLimitReached() const -> bool;
  void ApplyLowMemoryNotification();

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

 private:
  static void StaticGCCallback(v8::Isolate* isolate,
                               v8::GCType type,
                               v8::GCCallbackFlags flags,
                               void* data);
  void GCCallback(v8::Isolate* isolate);

  IsolateManager* isolate_manager_;
  size_t soft_memory_limit_;
  bool soft_memory_limit_reached_;
  size_t hard_memory_limit_;
  bool hard_memory_limit_reached_;
};

inline auto IsolateMemoryMonitor::IsSoftMemoryLimitReached() const -> bool {
  return soft_memory_limit_reached_;
}
inline auto IsolateMemoryMonitor::IsHardMemoryLimitReached() const -> bool {
  return hard_memory_limit_reached_;
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_MEMORY_MONITOR_H
