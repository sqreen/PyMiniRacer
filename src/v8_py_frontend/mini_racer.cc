#include "mini_racer.h"
#include <libplatform/libplatform.h>
#include <v8-initialization.h>
#include <v8-platform.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include "binary_value.h"
#include "cancelable_task_runner.h"

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
    : isolate_memory_monitor_(isolate_holder_.Get()),
      context_holder_(isolate_holder_.Get()),
      code_evaluator_(isolate_holder_.Get(),
                      context_holder_.Get(),
                      &bv_factory_,
                      &isolate_memory_monitor_),
      heap_reporter_(isolate_holder_.Get(), &bv_factory_),
      task_runner_(current_platform.get(), isolate_holder_.Get()),
      cancelable_task_runner_(&task_runner_) {}

auto Context::RunTask(std::function<BinaryValue::Ptr()> func,
                      MiniRacer::Callback callback,
                      void* cb_data) -> std::unique_ptr<CancelableTaskHandle> {
  return cancelable_task_runner_.Schedule<BinaryValue::Ptr>(
      /*runnable=*/
      std::move(func),
      /*on_completed=*/
      [callback, cb_data](BinaryValue::Ptr val) {
        callback(cb_data, val.release());
      },
      /*on_canceled=*/
      [callback, cb_data, this]() {
        callback(
            cb_data,
            bv_factory_.New("execution terminated", type_terminated_exception)
                .release());
      });
}

auto Context::Eval(const std::string& code,
                   MiniRacer::Callback callback,
                   void* cb_data) -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask([code, this]() { return code_evaluator_.Eval(code); },
                 callback, cb_data);
}

auto Context::HeapSnapshot(MiniRacer::Callback callback, void* cb_data)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask([this]() { return heap_reporter_.HeapSnapshot(); }, callback,
                 cb_data);
}

auto Context::HeapStats(MiniRacer::Callback callback, void* cb_data)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask([this]() { return heap_reporter_.HeapStats(); }, callback,
                 cb_data);
}

}  // end namespace MiniRacer
