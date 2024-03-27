#ifndef BINARY_VALUE_H
#define BINARY_VALUE_H

#include <v8-array-buffer.h>
#include <v8-context.h>
#include <v8-local-handle.h>
#include <v8-message.h>
#include <v8-value.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include "gsl_stub.h"

namespace MiniRacer {

enum BinaryTypes : uint8_t {
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
  BinaryValueDeleter() : factory_(nullptr) {}
  explicit BinaryValueDeleter(BinaryValueFactory* factory);
  void operator()(gsl::owner<BinaryValue*> val) const;

 private:
  BinaryValueFactory* factory_;
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
// NOLINTBEGIN(hicpp-member-init)
/** A simplified structure designed for sharing data with non-C++ code over a C
 * foreign function API (e.g., Python ctypes). */
struct BinaryValue {
  union {
    gsl::owner<void*> ptr_val;
    gsl::owner<char*> bytes;
    uint64_t int_val;
    double double_val;
  };
  size_t len;
  BinaryTypes type = type_invalid;

  BinaryValue() = default;

  BinaryValue(const std::string& str, BinaryTypes type);

  using Ptr = std::unique_ptr<BinaryValue, BinaryValueDeleter>;
} __attribute__((packed));
// NOLINTEND(hicpp-member-init)
// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(misc-non-private-member-variables-in-classes)

class BinaryValueFactory {
 public:
  auto FromString(std::string str, BinaryTypes result_type) -> BinaryValue::Ptr;
  auto FromValue(v8::Local<v8::Context> context,
                 v8::Local<v8::Value> value) -> BinaryValue::Ptr;
  auto FromExceptionMessage(v8::Local<v8::Context> context,
                            v8::Local<v8::Message> message,
                            v8::Local<v8::Value> exception_obj,
                            BinaryTypes result_type) -> BinaryValue::Ptr;

  // Only for use if the pointer is not wrapped in Ptr (see below), which uses
  // BinaryValueDeleter which calls this automatically:
  void Free(gsl::owner<BinaryValue*> val);

 private:
  auto New() -> BinaryValue::Ptr;
  std::unordered_map<void*, std::shared_ptr<v8::BackingStore>> backing_stores_;
};

inline BinaryValueDeleter::BinaryValueDeleter(BinaryValueFactory* factory)
    : factory_(factory) {}

inline void BinaryValueDeleter::operator()(gsl::owner<BinaryValue*> val) const {
  factory_->Free(val);
}

inline auto BinaryValueFactory::New() -> BinaryValue::Ptr {
  return {new BinaryValue(), BinaryValueDeleter(this)};
}

}  // namespace MiniRacer

#endif  // BINARY_VALUE_H
