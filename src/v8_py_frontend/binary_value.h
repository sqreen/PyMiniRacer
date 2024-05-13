#ifndef INCLUDE_MINI_RACER_BINARY_VALUE_H
#define INCLUDE_MINI_RACER_BINARY_VALUE_H

#include <v8-array-buffer.h>
#include <v8-context.h>
#include <v8-local-handle.h>
#include <v8-message.h>
#include <v8-persistent-handle.h>
#include <v8-value.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "isolate_object_collector.h"

namespace MiniRacer {

enum BinaryTypes : uint8_t {
  type_invalid = 0,
  type_null = 1,
  type_bool = 2,
  type_integer = 3,
  type_double = 4,
  type_str_utf8 = 5,
  type_array = 6,
  // type_hash      =   7,  // deprecated
  type_date = 8,
  type_symbol = 9,
  type_object = 10,
  type_undefined = 11,

  type_function = 100,
  type_shared_array_buffer = 101,
  type_array_buffer = 102,
  type_promise = 103,

  type_execute_exception = 200,
  type_parse_exception = 201,
  type_oom_exception = 202,
  type_timeout_exception = 203,
  type_terminated_exception = 204,
  type_value_exception = 205,
  type_key_exception = 206,
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
// NOLINTBEGIN(hicpp-member-init)
/** A simplified structure designed for sharing data with non-C++ code over a C
 * foreign function API (e.g., Python ctypes). This object directly provides
 * values for some simple types (e.g., numbers and strings), and also acts as a
 * handle for the non-C++ code to manage opaque data via our APIs. */
struct BinaryValueHandle {
  union {
    char* bytes;
    int64_t int_val;
    double double_val;
  };
  size_t len;
  BinaryTypes type = type_invalid;
} __attribute__((packed));
// NOLINTEND(hicpp-member-init)
// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(misc-non-private-member-variables-in-classes)

class BinaryValue {
 public:
  BinaryValue(IsolateObjectDeleter isolate_object_deleter,
              std::string_view val,
              BinaryTypes result_type);
  BinaryValue(IsolateObjectDeleter isolate_object_deleter, bool val);
  BinaryValue(IsolateObjectDeleter isolate_object_deleter,
              int64_t val,
              BinaryTypes result_type);
  BinaryValue(IsolateObjectDeleter isolate_object_deleter,
              double val,
              BinaryTypes result_type);
  BinaryValue(IsolateObjectDeleter isolate_object_deleter,
              v8::Local<v8::Context> context,
              v8::Local<v8::Value> value);
  BinaryValue(IsolateObjectDeleter isolate_object_deleter,
              v8::Local<v8::Context> context,
              v8::Local<v8::Message> message,
              v8::Local<v8::Value> exception_obj,
              BinaryTypes result_type);

  using Ptr = std::shared_ptr<BinaryValue>;

  auto ToValue(v8::Local<v8::Context> context) -> v8::Local<v8::Value>;

  friend class BinaryValueRegistry;

 private:
  auto GetHandle() -> BinaryValueHandle*;
  void SavePersistentHandle(v8::Isolate* isolate, v8::Local<v8::Value> value);
  void CreateBackingStoreRef(v8::Local<v8::Value> value);

  IsolateObjectDeleter isolate_object_deleter_;
  BinaryValueHandle handle_;
  std::vector<char> msg_;
  std::unique_ptr<v8::Persistent<v8::Value>, IsolateObjectDeleter>
      persistent_handle_;
  std::unique_ptr<std::shared_ptr<v8::BackingStore>, IsolateObjectDeleter>
      backing_store_;
};

class BinaryValueFactory {
 public:
  explicit BinaryValueFactory(IsolateObjectCollector* isolate_object_collector);

  template <typename... Params>
  auto New(Params&&... params) -> BinaryValue::Ptr;

 private:
  IsolateObjectCollector* isolate_object_collector_;
};

/** We return handles to BinaryValues to the MiniRacer user side (i.e.,
 * Python), as raw pointers. To ensure we keep those handles alive while Python
 * is using them, we register them in a map, contained within this class.
 */
class BinaryValueRegistry {
 public:
  /** Record the value in an internal map, so we don't destroy it when
   * returning a binary value handle to the MiniRacer user (i.e., the
   * Python side).
   */
  auto Remember(BinaryValue::Ptr ptr) -> BinaryValueHandle*;

  /** Unrecord a value so it can be garbage collected (once any other
   * shared_ptr references are dropped).
   */
  void Forget(BinaryValueHandle* handle);

  /** "Re-hydrate" a value from just its handle (only works if it was
   * "Remembered") */
  auto FromHandle(BinaryValueHandle* handle) -> BinaryValue::Ptr;

  /** Count the total number of remembered values, for test purposes. */
  auto Count() -> size_t;

 private:
  std::mutex mutex_;
  std::unordered_map<BinaryValueHandle*, std::shared_ptr<BinaryValue>> values_;
};

template <typename... Params>
inline auto BinaryValueFactory::New(Params&&... params) -> BinaryValue::Ptr {
  return std::make_shared<BinaryValue>(
      IsolateObjectDeleter(isolate_object_collector_),
      std::forward<Params>(params)...);
}

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_BINARY_VALUE_H
