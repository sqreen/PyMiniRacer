#include "context_factory.h"
#include <libplatform/libplatform.h>
#include <v8-initialization.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include "context.h"
#include "gsl_stub.h"

namespace MiniRacer {

gsl::owner<ContextFactory*> ContextFactory::singleton_ = nullptr;
std::once_flag ContextFactory::init_flag_;

void ContextFactory::Init(const std::string& v8_flags,
                          const std::filesystem::path& icu_path,
                          const std::filesystem::path& snapshot_path) {
  std::call_once(init_flag_, [v8_flags, icu_path, snapshot_path] {
    singleton_ = new ContextFactory(v8_flags, icu_path, snapshot_path);
  });
}

auto ContextFactory::Get() -> ContextFactory* {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return singleton_;
}

auto ContextFactory::MakeContext() -> uint64_t {
  // Actually create the context before we get the lock, in case the program is
  // making Contexts in other threads:
  auto context = std::make_shared<Context>(current_platform_.get());

  const std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t context_id = next_context_id_++;
  contexts_[context_id] = std::move(context);
  return context_id;
}

auto ContextFactory::GetContext(uint64_t context_id)
    -> std::shared_ptr<Context> {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto iter = contexts_.find(context_id);
  if (iter == contexts_.end()) {
    return {};
  }
  return iter->second;
}

void ContextFactory::FreeContext(uint64_t context_id) {
  std::shared_ptr<Context> context;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto iter = contexts_.find(context_id);
    if (iter == contexts_.end()) {
      return;
    }
    context = std::move(iter->second);
    contexts_.erase(iter);
  }
  // We actually destruct the context here, outside of the mutex, so that other
  // threads can continue to create, get, and free contexts.
}

auto ContextFactory::Count() -> size_t {
  const std::lock_guard<std::mutex> lock(mutex_);
  return contexts_.size();
}

ContextFactory::ContextFactory(const std::string& v8_flags,
                               const std::filesystem::path& icu_path,
                               const std::filesystem::path& snapshot_path)
    : next_context_id_(1) {
  v8::V8::InitializeICU(icu_path.string().c_str());
  v8::V8::InitializeExternalStartupDataFromFile(snapshot_path.string().c_str());

  if (!v8_flags.empty()) {
    v8::V8::SetFlagsFromString(v8_flags.c_str());
  }
  if (v8_flags.find("--single-threaded") != std::string::npos) {
    current_platform_ = v8::platform::NewSingleThreadedDefaultPlatform();
  } else {
    current_platform_ = v8::platform::NewDefaultPlatform();
  }
  v8::V8::InitializePlatform(current_platform_.get());
  v8::V8::Initialize();
}

}  // end namespace MiniRacer
