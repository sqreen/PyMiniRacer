#include <v8.h>
#include <map>
#include "binary_value.h"

namespace MiniRacer {

class Context;

class Context {
 public:
  Context();
  ~Context();

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  bool IsSoftMemoryLimitReached() { return soft_memory_limit_reached_; }
  bool IsHardMemoryLimitReached() { return hard_memory_limit_reached_; }
  void ApplyLowMemoryNotification() { isolate_->LowMemoryNotification(); }

  void FreeBinaryValue(BinaryValue* v) { bv_factory_.Free(v); }
  BinaryValue::Ptr HeapSnapshot();
  BinaryValue::Ptr HeapStats();
  BinaryValue::Ptr Eval(const std::string& code, unsigned long timeout);

 private:
  std::optional<std::string> ValueToUtf8String(v8::Local<v8::Value> value);

  static void StaticGCCallback(v8::Isolate* isolate,
                               v8::GCType type,
                               v8::GCCallbackFlags flags,
                               void* data);
  void GCCallback(v8::Isolate* isolate);
  BinaryValue::Ptr SummarizeTryCatch(v8::Local<v8::Context>& context,
                                     const v8::TryCatch& trycatch,
                                     BinaryTypes resultType);

  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_;
  v8::Persistent<v8::Context>* context_;
  BinaryValueFactory bv_factory_;
  size_t soft_memory_limit_;
  bool soft_memory_limit_reached_;
  size_t hard_memory_limit_;
  bool hard_memory_limit_reached_;
};

void init_v8(char const* v8_flags,
             char const* icu_path,
             char const* snapshot_path);

}  // namespace MiniRacer
