#ifndef MINI_RACER_H
#define MINI_RACER_H

#include <v8.h>
#include <filesystem>
#include "binary_value.h"
#include "code_evaluator.h"
#include "context_holder.h"
#include "heap_reporter.h"
#include "isolate_holder.h"
#include "isolate_memory_monitor.h"
#include "task_runner.h"

namespace MiniRacer {

class Context {
 public:
  Context();

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  auto IsSoftMemoryLimitReached() const -> bool;
  auto IsHardMemoryLimitReached() const -> bool;
  void ApplyLowMemoryNotification();

  void FreeBinaryValue(gsl::owner<BinaryValue*> val);
  auto HeapSnapshot() -> BinaryValue::Ptr;
  auto HeapStats() -> BinaryValue::Ptr;
  auto Eval(const std::string& code, uint64_t timeout) -> BinaryValue::Ptr;
  [[nodiscard]] auto FunctionEvalCallCount() const -> uint64_t;
  [[nodiscard]] auto FullEvalCallCount() const -> uint64_t;

 private:
  auto RunTask(std::function<BinaryValue::Ptr()> func) -> BinaryValue::Ptr;

  IsolateHolder isolate_holder_;
  IsolateMemoryMonitor isolate_memory_monitor_;
  BinaryValueFactory bv_factory_;
  ContextHolder context_holder_;
  CodeEvaluator code_evaluator_;
  HeapReporter heap_reporter_;
  TaskRunner task_runner_;
};

void init_v8(char const* v8_flags,
             const std::filesystem::path& icu_path,
             const std::filesystem::path& snapshot_path);

inline void Context::SetHardMemoryLimit(size_t limit) {
  isolate_memory_monitor_.SetHardMemoryLimit(limit);
}

inline void Context::SetSoftMemoryLimit(size_t limit) {
  isolate_memory_monitor_.SetSoftMemoryLimit(limit);
}

inline auto Context::IsSoftMemoryLimitReached() const -> bool {
  return isolate_memory_monitor_.IsSoftMemoryLimitReached();
}

inline auto Context::IsHardMemoryLimitReached() const -> bool {
  return isolate_memory_monitor_.IsHardMemoryLimitReached();
}

inline void Context::ApplyLowMemoryNotification() {
  isolate_memory_monitor_.ApplyLowMemoryNotification();
}

inline void Context::FreeBinaryValue(gsl::owner<BinaryValue*> val) {
  bv_factory_.Free(val);
}

inline auto Context::FunctionEvalCallCount() const -> uint64_t {
  return code_evaluator_.FunctionEvalCallCount();
}

inline auto Context::FullEvalCallCount() const -> uint64_t {
  return code_evaluator_.FullEvalCallCount();
}

}  // namespace MiniRacer

#endif  // MINI_RACER_H
