#ifndef INCLUDE_MINI_RACER_ISOLATE_MEMORY_MONITOR_H
#define INCLUDE_MINI_RACER_ISOLATE_MEMORY_MONITOR_H

#include <v8.h>

namespace MiniRacer {

class IsolateMemoryMonitor {
 public:
  IsolateMemoryMonitor(v8::Isolate* isolate);
  ~IsolateMemoryMonitor();

  bool IsSoftMemoryLimitReached() { return soft_memory_limit_reached_; }
  bool IsHardMemoryLimitReached() { return hard_memory_limit_reached_; }
  void ApplyLowMemoryNotification() { isolate_->LowMemoryNotification(); }

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

 private:
  static void StaticGCCallback(v8::Isolate* isolate,
                               v8::GCType type,
                               v8::GCCallbackFlags flags,
                               void* data);
  void GCCallback(v8::Isolate* isolate);

  v8::Isolate* isolate_;
  size_t soft_memory_limit_;
  bool soft_memory_limit_reached_;
  size_t hard_memory_limit_;
  bool hard_memory_limit_reached_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_MEMORY_MONITOR_H
