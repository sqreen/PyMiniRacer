#include <v8.h>

#include <map>

namespace MiniRacer {

enum BinaryTypes {
  type_invalid = 0,
  type_null = 1,
  type_bool = 2,
  type_integer = 3,
  type_double = 4,
  type_str_utf8 = 5,
  // type_array     =   6,  // deprecated
  // type_hash      =   7,  // deprecated
  type_date = 8,
  type_symbol = 9,
  type_object = 10,

  type_function = 100,
  type_shared_array_buffer = 101,
  type_array_buffer = 102,

  type_execute_exception = 200,
  type_parse_exception = 201,
  type_oom_exception = 202,
  type_timeout_exception = 203,
  type_terminated_exception = 204,
};

struct BinaryValue {
  union {
    void* ptr_val;
    char* bytes;
    uint64_t int_val;
    double double_val;
  };
  BinaryTypes type = type_invalid;
  size_t len;

  BinaryValue() {}

  BinaryValue(const std::string& str, BinaryTypes type)
      : type(type), len(str.size()) {
    bytes = new char[len + 1];
    std::copy(str.begin(), str.end(), bytes);
    bytes[len] = '\0';
  }
};

class Context;

class BinaryValueDeleter {
 public:
  BinaryValueDeleter() : mr_context_(0) {}
  BinaryValueDeleter(Context* mr_context) : mr_context_(mr_context) {}
  void operator()(BinaryValue* bv) const;

 private:
  Context* mr_context_;
};

typedef std::unique_ptr<BinaryValue, BinaryValueDeleter> BinaryValuePtr;

class Context {
 public:
  Context();
  ~Context();

  void SetHardMemoryLimit(size_t limit);
  void SetSoftMemoryLimit(size_t limit);

  bool IsSoftMemoryLimitReached() { return soft_memory_limit_reached_; }
  bool IsHardMemoryLimitReached() { return hard_memory_limit_reached_; }
  void ApplyLowMemoryNotification() { isolate_->LowMemoryNotification(); }

  void BinaryValueFree(BinaryValue* v);
  BinaryValuePtr HeapSnapshot();
  BinaryValuePtr HeapStats();
  BinaryValuePtr Eval(const std::string& code, unsigned long timeout);

 private:
  template <typename... Args>
  BinaryValuePtr MakeBinaryValue(Args&&... args);

  std::optional<std::string> ValueToUtf8String(v8::Local<v8::Value> value);

  static void StaticGCCallback(v8::Isolate* isolate,
                               v8::GCType type,
                               v8::GCCallbackFlags flags,
                               void* data);
  void GCCallback(v8::Isolate* isolate);
  BinaryValuePtr ConvertV8ToBinary(v8::Local<v8::Context> context,
                                   v8::Local<v8::Value> value);
  BinaryValuePtr SummarizeTryCatch(v8::Local<v8::Context>& context,
                                   const v8::TryCatch& trycatch,
                                   BinaryTypes resultType);

  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_;
  v8::Persistent<v8::Context>* context_;
  std::unordered_map<void*, std::shared_ptr<v8::BackingStore>> backing_stores_;
  size_t soft_memory_limit_;
  bool soft_memory_limit_reached_;
  size_t hard_memory_limit_;
  bool hard_memory_limit_reached_;
};

inline void BinaryValueDeleter::operator()(BinaryValue* bv) const {
  mr_context_->BinaryValueFree(bv);
}

template <typename... Args>
inline BinaryValuePtr Context::MakeBinaryValue(Args&&... args) {
  return BinaryValuePtr(new BinaryValue(std::forward<Args>(args)...),
                        BinaryValueDeleter(this));
}

void init_v8(char const* v8_flags,
             char const* icu_path,
             char const* snapshot_path);

}  // namespace MiniRacer
