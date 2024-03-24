#include "mini_racer.h"
#include <libplatform/libplatform.h>
#include <v8-initialization.h>
#include <v8-platform.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include "binary_value.h"

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
      task_runner_(current_platform.get(), isolate_holder_.Get()) {}

auto Context::RunTask(std::function<BinaryValue::Ptr()> func)
    -> BinaryValue::Ptr {
  std::promise<BinaryValue::Ptr> promise;
  std::future<BinaryValue::Ptr> future = promise.get_future();

  task_runner_.Run([&]() { promise.set_value(func()); });

  return future.get();
}

auto Context::Eval(const std::string& code,
                   uint64_t timeout) -> BinaryValue::Ptr {
  return RunTask([&]() { return code_evaluator_.Eval(code, timeout); });
}

auto Context::HeapSnapshot() -> BinaryValue::Ptr {
  return RunTask([&]() { return heap_reporter_.HeapSnapshot(); });
}

auto Context::HeapStats() -> BinaryValue::Ptr {
  return RunTask([&]() { return heap_reporter_.HeapStats(); });
}

}  // end namespace MiniRacer
