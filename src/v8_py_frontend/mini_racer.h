#ifndef MINI_RACER_H
#define MINI_RACER_H

#include <v8.h>
#include "binary_value.h"
#include "code_evaluator.h"
#include "heap_reporter.h"
#include "isolate_holder.h"
#include "isolate_memory_monitor.h"
#include "isolate_pump.h"

namespace MiniRacer {

class Context {
 public:
  Context();

  void SetHardMemoryLimit(size_t limit) {
    isolate_memory_monitor_.SetHardMemoryLimit(limit);
  }
  void SetSoftMemoryLimit(size_t limit) {
    isolate_memory_monitor_.SetSoftMemoryLimit(limit);
  }

  bool IsSoftMemoryLimitReached() {
    return isolate_memory_monitor_.IsSoftMemoryLimitReached();
  }
  bool IsHardMemoryLimitReached() {
    return isolate_memory_monitor_.IsHardMemoryLimitReached();
  }
  void ApplyLowMemoryNotification() {
    isolate_memory_monitor_.ApplyLowMemoryNotification();
  }

  void FreeBinaryValue(BinaryValue* binary_value);
  auto HeapSnapshot() -> BinaryValue::Ptr;
  auto HeapStats() -> BinaryValue::Ptr;
  auto Eval(const std::string& code, uint64_t timeout) -> BinaryValue::Ptr;

 private:
  BinaryValue::Ptr RunTask(std::function<BinaryValue::Ptr()> func);

  IsolateHolder isolate_holder_;
  IsolateMemoryMonitor isolate_memory_monitor_;
  BinaryValueFactory bv_factory_;
  CodeEvaluator code_evaluator_;
  HeapReporter heap_reporter_;
  IsolatePump isolate_pump_;
};

void init_v8(char const* v8_flags,
             const std::filesystem::path& icu_path,
             const std::filesystem::path& snapshot_path);

inline auto Context::IsSoftMemoryLimitReached() const -> bool {
  return soft_memory_limit_reached_;
}

inline auto Context::IsHardMemoryLimitReached() const -> bool {
  return hard_memory_limit_reached_;
}

inline void Context::ApplyLowMemoryNotification() {
  isolate_->LowMemoryNotification();
}

inline void Context::FreeBinaryValue(gsl::owner<BinaryValue*> binary_value) {
  bv_factory_.Free(binary_value);
}

}  // namespace MiniRacer

#endif  // MINI_RACER_H
