#ifndef MINI_RACER_H
#define MINI_RACER_H

#include <v8-persistent-handle.h>
#include <v8-value.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
#include "code_evaluator.h"
#include "context_holder.h"
#include "gsl_stub.h"
#include "heap_reporter.h"
#include "isolate_manager.h"
#include "isolate_memory_monitor.h"
#include "promise_attacher.h"

namespace MiniRacer {

class Context {
 public:
  Context();

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  [[nodiscard]] auto IsSoftMemoryLimitReached() const -> bool;
  [[nodiscard]] auto IsHardMemoryLimitReached() const -> bool;
  void ApplyLowMemoryNotification();

  void FreeBinaryValue(gsl::owner<BinaryValue*> val);
  auto HeapSnapshot(Callback callback,
                    void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;
  auto HeapStats(Callback callback,
                 void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;
  auto Eval(const std::string& code,
            Callback callback,
            void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;
  [[nodiscard]] auto FunctionEvalCallCount() const -> uint64_t;
  [[nodiscard]] auto FullEvalCallCount() const -> uint64_t;
  void AttachPromiseThen(v8::Persistent<v8::Value>* promise,
                         Callback callback,
                         void* cb_data);

 private:
  template <typename Runnable>
  auto RunTask(Runnable runnable,
               Callback callback,
               void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;

  IsolateManager isolate_manager_;
  IsolateMemoryMonitor isolate_memory_monitor_;
  BinaryValueFactory bv_factory_;
  ContextHolder context_holder_;
  CodeEvaluator code_evaluator_;
  HeapReporter heap_reporter_;
  PromiseAttacher promise_attacher_;
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

inline void Context::AttachPromiseThen(v8::Persistent<v8::Value>* promise,
                                       MiniRacer::Callback callback,
                                       void* cb_data) {
  promise_attacher_.AttachPromiseThen(promise, callback, cb_data);
}

}  // namespace MiniRacer

#endif  // MINI_RACER_H
