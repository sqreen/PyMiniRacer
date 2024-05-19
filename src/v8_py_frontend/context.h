#ifndef INCLUDE_MINI_RACER_CONTEXT_H
#define INCLUDE_MINI_RACER_CONTEXT_H

#include <v8-platform.h>
#include <cstddef>
#include <cstdint>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
#include "code_evaluator.h"
#include "context_holder.h"
#include "heap_reporter.h"
#include "isolate_manager.h"
#include "isolate_memory_monitor.h"
#include "isolate_object_collector.h"
#include "js_callback_maker.h"
#include "object_manipulator.h"

namespace MiniRacer {

class ValueHandleConverter;

class Context {
 public:
  explicit Context(v8::Platform* platform, Callback callback);
  ~Context();

  Context(const Context&) = delete;
  auto operator=(const Context&) -> Context& = delete;
  Context(Context&&) = delete;
  auto operator=(Context&& other) -> Context& = delete;

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  [[nodiscard]] auto IsSoftMemoryLimitReached() const -> bool;
  [[nodiscard]] auto IsHardMemoryLimitReached() const -> bool;
  void ApplyLowMemoryNotification();

  void FreeBinaryValue(BinaryValueHandle* val);
  template <typename... Params>
  auto AllocBinaryValue(Params&&... params) -> BinaryValueHandle*;
  void CancelTask(uint64_t task_id);
  auto HeapSnapshot(uint64_t callback_id) -> uint64_t;
  auto HeapStats(uint64_t callback_id) -> uint64_t;
  auto Eval(BinaryValueHandle* code_handle,

            uint64_t callback_id) -> uint64_t;
  auto MakeJSCallback(uint64_t callback_id) -> BinaryValueHandle*;
  auto GetIdentityHash(BinaryValueHandle* obj_handle) -> BinaryValueHandle*;
  auto GetOwnPropertyNames(BinaryValueHandle* obj_handle) -> BinaryValueHandle*;
  auto GetObjectItem(BinaryValueHandle* obj_handle,
                     BinaryValueHandle* key_handle) -> BinaryValueHandle*;
  auto SetObjectItem(BinaryValueHandle* obj_handle,
                     BinaryValueHandle* key_handle,
                     BinaryValueHandle* val_handle) -> BinaryValueHandle*;
  auto DelObjectItem(BinaryValueHandle* obj_handle,
                     BinaryValueHandle* key_handle) -> BinaryValueHandle*;
  auto SpliceArray(BinaryValueHandle* obj_handle,
                   int32_t start,
                   int32_t delete_count,
                   BinaryValueHandle* new_val_handle) -> BinaryValueHandle*;
  auto CallFunction(BinaryValueHandle* func_handle,
                    BinaryValueHandle* this_handle,
                    BinaryValueHandle* argv_handle,

                    uint64_t callback_id) -> uint64_t;
  auto BinaryValueCount() -> size_t;

 private:
  template <typename Runnable>
  auto RunTask(Runnable runnable, uint64_t callback_id) -> uint64_t;

  auto MakeHandleConverter(BinaryValueHandle* handle,
                           const char* err_msg) -> ValueHandleConverter;

  IsolateManager isolate_manager_;
  IsolateObjectCollector isolate_object_collector_;
  IsolateMemoryMonitor isolate_memory_monitor_;
  BinaryValueFactory bv_factory_;
  BinaryValueRegistry bv_registry_;
  RememberValueAndCallback callback_;
  ContextHolder context_holder_;
  JSCallbackMaker js_callback_maker_;
  CodeEvaluator code_evaluator_;
  HeapReporter heap_reporter_;
  ObjectManipulator object_manipulator_;
  CancelableTaskManager cancelable_task_manager_;
};

class ValueHandleConverter {
 public:
  ValueHandleConverter(BinaryValueFactory* bv_factory,
                       BinaryValueRegistry* bv_registry,
                       BinaryValueHandle* handle,
                       const char* err_msg);

  explicit operator bool() const;

  auto GetErrorPtr() -> BinaryValue::Ptr;
  auto GetErrorHandle() -> BinaryValueHandle*;
  auto GetPtr() -> BinaryValue::Ptr;

 private:
  BinaryValueRegistry* bv_registry_;
  BinaryValue::Ptr ptr_;
  BinaryValue::Ptr err_;
};

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

template <typename... Params>
inline auto Context::AllocBinaryValue(Params&&... params)
    -> BinaryValueHandle* {
  return bv_registry_.Remember(
      bv_factory_.New(std::forward<Params>(params)...));
}

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CONTEXT_H
