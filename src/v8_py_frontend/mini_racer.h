#ifndef MINI_RACER_H
#define MINI_RACER_H

#include <v8.h>
#include <filesystem>
#include <map>
#include <optional>
#include "binary_value.h"

namespace MiniRacer {

class Context {
 public:
  Context();
  ~Context();

  Context(const Context&) = delete;
  auto operator=(const Context&) -> Context& = delete;
  Context(Context&&) = delete;
  auto operator=(Context&& other) -> Context& = delete;

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  auto IsSoftMemoryLimitReached() const -> bool;
  auto IsHardMemoryLimitReached() const -> bool;
  void ApplyLowMemoryNotification();

  void FreeBinaryValue(BinaryValue* binary_value);
  auto HeapSnapshot() -> BinaryValue::Ptr;
  auto HeapStats() -> BinaryValue::Ptr;
  auto Eval(const std::string& code, uint64_t timeout) -> BinaryValue::Ptr;

 private:
  auto ValueToUtf8String(v8::Local<v8::Value> value)
      -> std::optional<std::string>;

  static void StaticGCCallback(v8::Isolate* isolate,
                               v8::GCType type,
                               v8::GCCallbackFlags flags,
                               void* data);
  void GCCallback(v8::Isolate* isolate);
  auto SummarizeTryCatch(v8::Local<v8::Context>& context,
                         const v8::TryCatch& trycatch,
                         BinaryTypes resultType) -> BinaryValue::Ptr;

  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_;
  std::unique_ptr<v8::Persistent<v8::Context>> context_;
  BinaryValueFactory bv_factory_;
  size_t soft_memory_limit_;
  bool soft_memory_limit_reached_;
  size_t hard_memory_limit_;
  bool hard_memory_limit_reached_;
};

void init_v8(char const* v8_flags,
             const std::filesystem::path& icu_path,
             const std::filesystem::path& snapshot_path);

inline auto Context::IsSoftMemoryLimitReached() const -> bool {
  return soft_memory_limit_reached_;
}

inline auto Context::IsHardMemoryLimitReached() const -> bool {
  return hard_memory_limit_reached_;
}

inline void Context::ApplyLowMemoryNotification() {
  isolate_->LowMemoryNotification();
}

inline void Context::FreeBinaryValue(gsl::owner<BinaryValue*> binary_value) {
  bv_factory_.Free(binary_value);
}

}  // namespace MiniRacer

#endif  // MINI_RACER_H
