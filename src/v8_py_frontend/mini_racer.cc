#include "mini_racer.h"
#include <libplatform/libplatform.h>
#include <v8-initialization.h>
#include <v8-locker.h>
#include <v8-persistent-handle.h>
#include <v8-platform.h>
#include <v8-value.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
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
      isolate_memory_monitor_(&isolate_manager_),
      context_holder_(&isolate_manager_),
      code_evaluator_(context_holder_.Get(),
                      &bv_factory_,
                      &isolate_memory_monitor_),
      heap_reporter_(&bv_factory_),
      promise_attacher_(&isolate_manager_, context_holder_.Get(), &bv_factory_),
      object_manipulator_(context_holder_.Get(), &bv_factory_),
      cancelable_task_runner_(&isolate_manager_) {}

template <typename Runnable>
auto Context::RunTask(Runnable runnable,
                      Callback callback,
                      void* cb_data) -> std::unique_ptr<CancelableTaskHandle> {
  return cancelable_task_runner_.Schedule(
      /*runnable=*/
      std::move(runnable),
      /*on_completed=*/
      [callback, cb_data](BinaryValue::Ptr val) {
        callback(cb_data, val.release());
      },
      /*on_canceled=*/
      [callback, cb_data, this]() {
        callback(cb_data, bv_factory_
                              .FromString("execution terminated",
                                          type_terminated_exception)
                              .release());
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

auto Context::GetIdentityHash(v8::Persistent<v8::Value>* object) -> int {
  return isolate_manager_.RunAndAwait([object](v8::Isolate* isolate) {
    return ObjectManipulator::GetIdentityHash(isolate, object);
  });
}

auto Context::GetOwnPropertyNames(v8::Persistent<v8::Value>* object)
    -> BinaryValue::Ptr {
  return isolate_manager_.RunAndAwait([object, this](v8::Isolate* isolate) {
    return object_manipulator_.GetOwnPropertyNames(isolate, object);
  });
}

auto Context::GetObjectItem(v8::Persistent<v8::Value>* object,
                            BinaryValue* key) -> BinaryValue::Ptr {
  return isolate_manager_.RunAndAwait(
      [object, this, &key](v8::Isolate* isolate) mutable {
        return object_manipulator_.Get(isolate, object, key);
      });
}

void Context::SetObjectItem(v8::Persistent<v8::Value>* object,
                            BinaryValue* key,
                            BinaryValue* val) {
  isolate_manager_.RunAndAwait(
      [object, this, &key, val](v8::Isolate* isolate) mutable {
        object_manipulator_.Set(isolate, object, key, val);
      });
}

auto Context::DelObjectItem(v8::Persistent<v8::Value>* object,
                            BinaryValue* key) -> bool {
  return isolate_manager_.RunAndAwait(
      [object, this, &key](v8::Isolate* isolate) mutable {
        return object_manipulator_.Del(isolate, object, key);
      });
}

auto Context::SpliceArray(v8::Persistent<v8::Value>* object,
                          int32_t start,
                          int32_t delete_count,
                          BinaryValue* new_val) -> BinaryValue::Ptr {
  return isolate_manager_.RunAndAwait(
      [object, this, start, delete_count, new_val](v8::Isolate* isolate) {
        return object_manipulator_.Splice(isolate, object, start, delete_count,
                                          new_val);
      });
}

}  // end namespace MiniRacer
