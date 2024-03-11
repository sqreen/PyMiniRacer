#include <libplatform/libplatform.h>
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
  BinaryValueDeleter() : mr_context(0) {}
  BinaryValueDeleter(Context* mr_context) : mr_context(mr_context) {}
  void operator()(BinaryValue* bv) const;

 private:
  Context* mr_context;
};

typedef std::unique_ptr<BinaryValue, BinaryValueDeleter> BinaryValuePtr;

class Context {
 public:
  Context();
  ~Context();

  v8::Isolate* isolate;
  v8::Persistent<v8::Context>* persistentContext;
  v8::ArrayBuffer::Allocator* allocator;
  std::map<void*, std::shared_ptr<v8::BackingStore>> backing_stores;
  size_t soft_memory_limit;
  bool soft_memory_limit_reached;
  size_t hard_memory_limit;
  bool hard_memory_limit_reached;

  template <typename... Args>
  BinaryValuePtr makeBinaryValue(Args&&... args);

  void BinaryValueFree(BinaryValue* v);

  std::optional<std::string> valueToUtf8String(v8::Local<v8::Value> value);

  static void static_gc_callback(v8::Isolate* isolate,
                                 v8::GCType type,
                                 v8::GCCallbackFlags flags,
                                 void* data);
  void gc_callback(v8::Isolate* isolate);
  void set_hard_memory_limit(size_t limit);
  void set_soft_memory_limit(size_t limit);
  BinaryValuePtr convert_v8_to_binary(v8::Local<v8::Context> context,
                                      v8::Local<v8::Value> value);
  BinaryValuePtr heap_snapshot();
  BinaryValuePtr heap_stats();
  BinaryValuePtr eval(const std::string& code, unsigned long timeout);
  BinaryValuePtr summarizeTryCatch(v8::Local<v8::Context>& context,
                                   v8::TryCatch& trycatch,
                                   BinaryTypes resultType);
};

inline void BinaryValueDeleter::operator()(BinaryValue* bv) const {
  mr_context->BinaryValueFree(bv);
}

template <typename... Args>
inline BinaryValuePtr Context::makeBinaryValue(Args&&... args) {
  return BinaryValuePtr(new BinaryValue(std::forward<Args>(args)...),
                        BinaryValueDeleter(this));
}

void init_v8(char const* v8_flags,
             char const* icu_path,
             char const* snapshot_path);

}  // namespace MiniRacer
