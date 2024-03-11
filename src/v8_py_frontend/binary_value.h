#include <v8.h>

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

struct BinaryValue;
class BinaryValueFactory;

class BinaryValueDeleter {
 public:
  BinaryValueDeleter() : factory_(0) {}
  BinaryValueDeleter(BinaryValueFactory* factory) : factory_(factory) {}
  void operator()(BinaryValue* bv) const;

 private:
  BinaryValueFactory* factory_;
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

  typedef std::unique_ptr<BinaryValue, BinaryValueDeleter> Ptr;
};

class BinaryValueFactory {
 public:
  template <typename... Args>
  BinaryValue::Ptr New(Args&&... args);

  BinaryValue::Ptr ConvertFromV8(v8::Local<v8::Context> context,
                                 v8::Local<v8::Value> value);

  // Only for use if the pointer is not wrapped in Ptr (see below), which uses
  // BinaryValueDeleter which calls this automatically:
  void Free(BinaryValue* v);

  void Clear() { backing_stores_.clear(); }

 private:
  std::unordered_map<void*, std::shared_ptr<v8::BackingStore>> backing_stores_;
};

inline void BinaryValueDeleter::operator()(BinaryValue* bv) const {
  factory_->Free(bv);
}

template <typename... Args>
inline BinaryValue::Ptr BinaryValueFactory::New(Args&&... args) {
  return BinaryValue::Ptr(new BinaryValue(std::forward<Args>(args)...),
                          BinaryValueDeleter(this));
}

}  // namespace MiniRacer
