#include "mini_racer.h"
#include <libplatform/libplatform.h>
#include <v8-initialization.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-persistent-handle.h>
#include <v8-platform.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
#include "gsl_stub.h"
#include "object_manipulator.h"

namespace MiniRacer {

namespace {
// V8 inherently needs a singleton, so disable associated linter errors:
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(fuchsia-statically-constructed-objects)
std::unique_ptr<v8::Platform> current_platform = nullptr;
// NOLINTEND(fuchsia-statically-constructed-objects)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
}  // end anonymous namespace

void init_v8(const std::string& v8_flags,
             const std::filesystem::path& icu_path,
             const std::filesystem::path& snapshot_path) {
  v8::V8::InitializeICU(icu_path.string().c_str());
  v8::V8::InitializeExternalStartupDataFromFile(snapshot_path.string().c_str());

  if (!v8_flags.empty()) {
    v8::V8::SetFlagsFromString(v8_flags.c_str());
  }
  if (v8_flags.find("--single-threaded") != std::string::npos) {
    current_platform = v8::platform::NewSingleThreadedDefaultPlatform();
  } else {
    current_platform = v8::platform::NewDefaultPlatform();
  }
  v8::V8::InitializePlatform(current_platform.get());
  v8::V8::Initialize();
}

Context::Context()
    : isolate_manager_(current_platform.get()),
      isolate_manager_stopper_(&isolate_manager_),
      isolate_memory_monitor_(&isolate_manager_),
      bv_factory_(&isolate_manager_),
      context_holder_(&isolate_manager_),
      code_evaluator_(context_holder_.Get(),
                      &bv_factory_,
                      &isolate_memory_monitor_),
      heap_reporter_(&bv_factory_),
      promise_attacher_(&isolate_manager_, context_holder_.Get(), &bv_factory_),
      object_manipulator_(context_holder_.Get(), &bv_factory_),
      cancelable_task_runner_(&isolate_manager_),
      pending_task_waiter_(&pending_task_counter_) {}

template <typename Runnable>
auto Context::RunTask(Runnable runnable,
                      Callback callback,
                      void* cb_data) -> std::unique_ptr<CancelableTaskHandle> {
  // Start an async task!

  // To make sure we perform an orderly exit, we track this async work, and
  // wait for it to complete before we start destructing the Context:
  pending_task_counter_.Increment();

  return cancelable_task_runner_.Schedule(
      /*runnable=*/
      std::move(runnable),
      /*on_completed=*/
      [callback, cb_data, this](BinaryValue::Ptr val) {
        pending_task_counter_.Decrement();
        callback(cb_data, val.release());
      },
      /*on_canceled=*/
      [callback, cb_data, this]() {
        auto err = bv_factory_.FromString("execution terminated",
                                          type_terminated_exception);
        pending_task_counter_.Decrement();
        callback(cb_data, err.release());
      });
}

auto Context::Eval(const std::string& code,
                   Callback callback,
                   void* cb_data) -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [code, this](v8::Isolate* isolate) {
        return code_evaluator_.Eval(isolate, code);
      },
      callback, cb_data);
}

auto Context::HeapSnapshot(Callback callback, void* cb_data)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [this](v8::Isolate* isolate) {
        return heap_reporter_.HeapSnapshot(isolate);
      },
      callback, cb_data);
}

auto Context::HeapStats(Callback callback, void* cb_data)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [this](v8::Isolate* isolate) {
        return heap_reporter_.HeapStats(isolate);
      },
      callback, cb_data);
}

auto Context::GetIdentityHash(BinaryValue* bv_ptr) -> int {
  return isolate_manager_.RunAndAwait([bv_ptr, this](v8::Isolate* isolate) {
    const v8::HandleScope handle_scope(isolate);
    return ObjectManipulator::GetIdentityHash(
        isolate, bv_factory_.GetPersistentHandle(isolate, bv_ptr));
  });
}

auto Context::GetOwnPropertyNames(BinaryValue* bv_ptr) -> BinaryValue::Ptr {
  return isolate_manager_.RunAndAwait([bv_ptr, this](v8::Isolate* isolate) {
    const v8::HandleScope handle_scope(isolate);
    return object_manipulator_.GetOwnPropertyNames(
        isolate, bv_factory_.GetPersistentHandle(isolate, bv_ptr));
  });
}

auto Context::GetObjectItem(BinaryValue* bv_ptr,
                            BinaryValue* key) -> BinaryValue::Ptr {
  return isolate_manager_.RunAndAwait(
      [bv_ptr, this, &key](v8::Isolate* isolate) mutable {
        const v8::HandleScope handle_scope(isolate);
        return object_manipulator_.Get(
            isolate, bv_factory_.GetPersistentHandle(isolate, bv_ptr), key);
      });
}

void Context::SetObjectItem(BinaryValue* bv_ptr,
                            BinaryValue* key,
                            BinaryValue* val) {
  isolate_manager_.RunAndAwait([bv_ptr, this, &key,
                                val](v8::Isolate* isolate) mutable {
    const v8::HandleScope handle_scope(isolate);
    object_manipulator_.Set(
        isolate, bv_factory_.GetPersistentHandle(isolate, bv_ptr), key, val);
  });
}

auto Context::DelObjectItem(BinaryValue* bv_ptr, BinaryValue* key) -> bool {
  return isolate_manager_.RunAndAwait(
      [bv_ptr, this, &key](v8::Isolate* isolate) mutable {
        const v8::HandleScope handle_scope(isolate);
        return object_manipulator_.Del(
            isolate, bv_factory_.GetPersistentHandle(isolate, bv_ptr), key);
      });
}

auto Context::SpliceArray(BinaryValue* bv_ptr,
                          int32_t start,
                          int32_t delete_count,
                          BinaryValue* new_val) -> BinaryValue::Ptr {
  return isolate_manager_.RunAndAwait(
      [bv_ptr, this, start, delete_count, new_val](v8::Isolate* isolate) {
        const v8::HandleScope handle_scope(isolate);
        return object_manipulator_.Splice(
            isolate, bv_factory_.GetPersistentHandle(isolate, bv_ptr), start,
            delete_count, new_val);
      });
}

void Context::FreeBinaryValue(gsl::owner<BinaryValue*> val) {
  bv_factory_.Free(val);
}

auto Context::CallFunction(BinaryValue* func_ptr,
                           BinaryValue* this_ptr,
                           BinaryValue* argv,
                           Callback callback,
                           void* cb_data)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [func_ptr, this, this_ptr, argv](v8::Isolate* isolate) {
        const v8::HandleScope handle_scope(isolate);
        return object_manipulator_.Call(
            isolate, bv_factory_.GetPersistentHandle(isolate, func_ptr),
            this_ptr, argv);
      },
      callback, cb_data);
}

}  // end namespace MiniRacer
