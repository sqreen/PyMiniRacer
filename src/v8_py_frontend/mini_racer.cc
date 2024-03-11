#include "mini_racer.h"
#include <libplatform/libplatform.h>
#include <future>

namespace MiniRacer {

namespace {
// V8 inherently needs a singleton, so disable associated linter errors:
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(fuchsia-statically-constructed-objects)
std::unique_ptr<v8::Platform> current_platform = nullptr;
// NOLINTEND(fuchsia-statically-constructed-objects)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
}  // end anonymous namespace

void init_v8(char const* v8_flags,
             const std::filesystem::path& icu_path,
             const std::filesystem::path& snapshot_path) {
  v8::V8::InitializeICU(icu_path.string().c_str());
  v8::V8::InitializeExternalStartupDataFromFile(snapshot_path.string().c_str());

  if (v8_flags != nullptr) {
    v8::V8::SetFlagsFromString(v8_flags);
  }
  if (v8_flags != nullptr && strstr(v8_flags, "--single-threaded") != nullptr) {
    current_platform = v8::platform::NewSingleThreadedDefaultPlatform();
  } else {
    current_platform = v8::platform::NewDefaultPlatform();
  }
  v8::V8::InitializePlatform(current_platform.get());
  v8::V8::Initialize();
}

Context::Context()
    : isolate_holder_(),
      isolate_memory_monitor_(isolate_holder_.Get()),
      code_evaluator_(isolate_holder_.Get(),
                      &bv_factory_,
                      &isolate_memory_monitor_),
      heap_reporter_(isolate_holder_.Get(), &bv_factory_),
      isolate_pump_(current_platform.get(), isolate_holder_.Get()) {}

BinaryValue::Ptr Context::RunTask(std::function<BinaryValue::Ptr()> func) {
  std::promise<BinaryValue::Ptr> promise;
  std::future<BinaryValue::Ptr> future = promise.get_future();

  isolate_pump_.RunInForegroundRunner([&]() { promise.set_value(func()); });

  return future.get();
}

BinaryValue::Ptr Context::Eval(const std::string& code, unsigned long timeout) {
  return RunTask([&]() { return code_evaluator_.Eval(code, timeout); });
}

BinaryValue::Ptr Context::HeapSnapshot() {
  return RunTask([&]() { return heap_reporter_.HeapSnapshot(); });
}

BinaryValue::Ptr Context::HeapStats() {
  return RunTask([&]() { return heap_reporter_.HeapStats(); });
}

}  // end namespace MiniRacer
