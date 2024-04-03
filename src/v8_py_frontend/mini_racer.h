#ifndef MINI_RACER_H
#define MINI_RACER_H

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
#include "count_down_latch.h"
#include "gsl_stub.h"
#include "heap_reporter.h"
#include "isolate_manager.h"
#include "isolate_memory_monitor.h"
#include "object_manipulator.h"
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
  void AttachPromiseThen(BinaryValue* bv_ptr, Callback callback, void* cb_data);
  auto GetIdentityHash(BinaryValue* bv_ptr) -> int;
  auto GetOwnPropertyNames(BinaryValue* bv_ptr) -> BinaryValue::Ptr;
  auto GetObjectItem(BinaryValue* bv_ptr, BinaryValue* key) -> BinaryValue::Ptr;
  void SetObjectItem(BinaryValue* bv_ptr, BinaryValue* key, BinaryValue* val);
  auto DelObjectItem(BinaryValue* bv_ptr, BinaryValue* key) -> bool;
  auto SpliceArray(BinaryValue* bv_ptr,
                   int32_t start,
                   int32_t delete_count,
                   BinaryValue* new_val) -> BinaryValue::Ptr;
  auto CallFunction(BinaryValue* func_ptr,
                    BinaryValue* this_ptr,
                    BinaryValue* argv,
                    Callback callback,
                    void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;

 private:
  template <typename Runnable>
  auto RunTask(Runnable runnable,
               Callback callback,
               void* cb_data) -> std::unique_ptr<CancelableTaskHandle>;

  IsolateManager isolate_manager_;
  IsolateManagerStopper isolate_manager_stopper_;
  IsolateMemoryMonitor isolate_memory_monitor_;
  BinaryValueFactory bv_factory_;
  ContextHolder context_holder_;
  CodeEvaluator code_evaluator_;
  HeapReporter heap_reporter_;
  PromiseAttacher promise_attacher_;
  ObjectManipulator object_manipulator_;
  CancelableTaskRunner cancelable_task_runner_;
  CountDownLatch pending_task_counter_;
  CountDownLatchWaiter pending_task_waiter_;
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

inline void Context::AttachPromiseThen(BinaryValue* bv_ptr,
                                       MiniRacer::Callback callback,
                                       void* cb_data) {
  promise_attacher_.AttachPromiseThen(bv_ptr, callback, cb_data);
}

}  // namespace MiniRacer

#endif  // MINI_RACER_H
