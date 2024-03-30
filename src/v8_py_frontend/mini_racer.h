#ifndef MINI_RACER_H
#define MINI_RACER_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "binary_value.h"
#include "cancelable_task_runner.h"
#include "code_evaluator.h"
#include "context_holder.h"
#include "gsl_stub.h"
#include "heap_reporter.h"
#include "isolate_manager.h"
#include "isolate_memory_monitor.h"

namespace MiniRacer {

using Callback = void (*)(void*, BinaryValue*);

class Context {
 public:
  Context();

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  [[nodiscard]] auto IsSoftMemoryLimitReached() const -> bool;
  [[nodiscard]] auto IsHardMemoryLimitReached() const -> bool;
  void ApplyLowMemoryNotification();

  void FreeBinaryValue(gsl::owner<BinaryValue*> val);
  auto HeapSnapshot(MiniRacer::Callback callback,
                    void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;
  auto HeapStats(MiniRacer::Callback callback,
                 void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;
  auto Eval(const std::string& code,
            MiniRacer::Callback callback,
            void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;
  [[nodiscard]] auto FunctionEvalCallCount() const -> uint64_t;
  [[nodiscard]] auto FullEvalCallCount() const -> uint64_t;

 private:
  template <typename Runnable>
  auto RunTask(Runnable runnable,
               MiniRacer::Callback callback,
               void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;

  IsolateManager isolate_manager_;
  IsolateMemoryMonitor isolate_memory_monitor_;
  BinaryValueFactory bv_factory_;
  ContextHolder context_holder_;
  CodeEvaluator code_evaluator_;
  HeapReporter heap_reporter_;
  CancelableTaskRunner cancelable_task_runner_;
};

void init_v8(const std::string& v8_flags,
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
