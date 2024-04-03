#include "binary_value.h"

#include <v8-array-buffer.h>
#include <v8-date.h>
#include <v8-exception.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-value.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include "gsl_stub.h"
#include "isolate_manager.h"

namespace MiniRacer {

BinaryValueFactory::BinaryValueFactory(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager) {}

BinaryValueFactory::~BinaryValueFactory() {
  // Free any binary values which Python didn't free before destroying the
  // MiniRacer::Context:
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  for (gsl::owner<BinaryValue*> bv_ptr : binary_values_) {
    DoFree(bv_ptr);
  }
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

void BinaryValueFactory::Free(gsl::owner<BinaryValue*> val) {
  {
    const std::lock_guard<std::mutex> lock(binary_values_mutex_);
    binary_values_.erase(val);
  }

  DoFree(val);
}

void BinaryValueFactory::DoFree(gsl::owner<BinaryValue*> val) {
  if (val == nullptr) {
    return;
  }
  switch (val->type) {
    // We represent these types as byte arrays in the union:
    case type_execute_exception:
    case type_parse_exception:
    case type_oom_exception:
    case type_timeout_exception:
    case type_terminated_exception:
    case type_str_utf8:
      delete[] val->bytes;
      break;
    // We represent these types as scalar values embedded in the union, so
    // there's nothing extra to free:
    case type_bool:
    case type_double:
    case type_date:
    case type_null:
    case type_undefined:
    case type_integer:
    case type_invalid:
      break;
    // We represent these types as pointers *into* a v8::BackingStore, and we
    // maintain a map of backing stores separately:
    case type_shared_array_buffer:
    case type_array_buffer:
      DeleteBackingStoreRef(val);
      break;
    // We represent these types as identifiers pointing into a map of
    // v8::Persistent handles:
    case type_symbol:
    case type_function:
    case type_object:
    case type_array:
    case type_promise:
      DeletePersistentHandle(val);
      break;
  }
  delete val;
}

auto BinaryValueFactory::FromValue(v8::Local<v8::Context> context,
                                   v8::Local<v8::Value> value)
    -> BinaryValue::Ptr {
  BinaryValue::Ptr res = New();

  if (value->IsNull()) {
    res->type = type_null;
  } else if (value->IsUndefined()) {
    res->type = type_undefined;
  } else if (value->IsInt32()) {
    res->type = type_integer;
    auto val = value->Int32Value(context).ToChecked();
    res->int_val = val;
  }
  // ECMA-262, 4.3.20
  // http://www.ecma-international.org/ecma-262/5.1/#sec-4.3.19
  else if (value->IsNumber()) {
    res->type = type_double;
    const double val = value->NumberValue(context).ToChecked();
    res->double_val = val;
  } else if (value->IsBoolean()) {
    res->type = type_bool;
    res->int_val = (value->IsTrue() ? 1 : 0);
  } else if (value->IsFunction()) {
    res->type = type_function;
    SavePersistentHandle(context->GetIsolate(), res.get(), value);
  } else if (value->IsSymbol()) {
    res->type = type_symbol;
    SavePersistentHandle(context->GetIsolate(), res.get(), value);
  } else if (value->IsDate()) {
    res->type = type_date;
    const v8::Local<v8::Date> date = v8::Local<v8::Date>::Cast(value);

    const double timestamp = date->ValueOf();
    res->double_val = timestamp;
  } else if (value->IsString()) {
    const v8::Local<v8::String> rstr =
        value->ToString(context).ToLocalChecked();

    res->type = type_str_utf8;
    res->len = static_cast<size_t>(
        rstr->Utf8Length(context->GetIsolate()));  // in bytes
    const size_t capacity = res->len + 1;
    res->bytes = new char[capacity];
    rstr->WriteUtf8(context->GetIsolate(), res->bytes);
  } else if (value->IsSharedArrayBuffer() || value->IsArrayBuffer() ||
             value->IsArrayBufferView()) {
    CreateBackingStoreRef(value, res.get());
  } else if (value->IsPromise()) {
    res->type = type_promise;
    SavePersistentHandle(context->GetIsolate(), res.get(), value);
  } else if (value->IsArray()) {
    res->type = type_array;
    SavePersistentHandle(context->GetIsolate(), res.get(), value);
  } else if (value->IsObject()) {
    res->type = type_object;
    SavePersistentHandle(context->GetIsolate(), res.get(), value);
  } else {
    return {};
  }
  return res;
}

auto BinaryValueFactory::New() -> BinaryValue::Ptr {
  auto ret = BinaryValue::Ptr(new BinaryValue(), BinaryValueDeleter(this));

  {
    // Track all created binary values to relieve Python of the duty of garbage
    // collecting them in the correct order relative to the MiniRacer::Context:
    const std::lock_guard<std::mutex> lock(binary_values_mutex_);
    binary_values_.insert(ret.get());
  }

  return ret;
}

void BinaryValueFactory::SavePersistentHandle(v8::Isolate* isolate,
                                              BinaryValue* bv_ptr,
                                              v8::Local<v8::Value> value) {
  // We store a map from BinaryValue* to v8::Persistent* rather than exposing
  // the latter to the Python side, because this makes it easier to reason
  // about memory management. The Python side only sees the BinaryValue
  // pointer, and freeing it (via BinaryValueFactory::Free) takes care of the
  // hidden underlying v8::Persistent* automatically.
  const std::lock_guard<std::mutex> lock(persistent_handles_mutex_);
  persistent_handles_[bv_ptr] =
      std::make_unique<v8::Persistent<v8::Value>>(isolate, value);
}

auto BinaryValueFactory::GetPersistentHandle(v8::Isolate* isolate,
                                             BinaryValue* bv_ptr)
    -> v8::Local<v8::Value> {
  const std::lock_guard<std::mutex> lock(persistent_handles_mutex_);
  auto iter = persistent_handles_.find(bv_ptr);
  if (iter == persistent_handles_.end()) {
    return {};
  }
  return iter->second->Get(isolate);
}

void BinaryValueFactory::DeletePersistentHandle(BinaryValue* bv_ptr) {
  const std::lock_guard<std::mutex> lock(persistent_handles_mutex_);
  auto iter = persistent_handles_.find(bv_ptr);
  if (iter == persistent_handles_.end()) {
    return;
  }

  auto persistent_handle = std::move(iter->second);
  persistent_handles_.erase(iter);

  // We don't generally own the isolate lock (i.e., aren't running from the
  // IsolateManager's message loop) here. From the V8 documentation, it's not
  // clear if we can safely free a v8::Persistent handle without the lock
  // (As a rule, messing with Isolate-owned objects without holding the Isolate
  // lock is not safe, and there is no documentation indicating
  // v8::Persistent::~Persistent is exempt from this rule.)
  // So let's have the message loop handle the deletion.
  // Note that we don't wait on the deletion here.
  // This method may be called by Python, as a result of callbacks from the C++
  // side of MiniRacer. Those callbacks originate from the IsolateManager
  // message loop itself. If we were to wait on this *new* task we're adding to
  // the message loop, we would deadlock.
  isolate_manager_->Run([ptr = std::move(persistent_handle)](
                            v8::Isolate* /*isolate*/) mutable { ptr.reset(); });
}

void BinaryValueFactory::CreateBackingStoreRef(v8::Local<v8::Value> value,
                                               BinaryValue* bv_ptr) {
  // For ArrayBuffer and friends, we store a reference to the ArrayBuffer
  // shared_ptr in this BinaryValueFactory instance, and return a pointer
  // *into* the buffer to the Python side.

  std::shared_ptr<v8::BackingStore> backing_store;
  size_t offset = 0;
  size_t size = 0;

  if (value->IsArrayBufferView()) {
    const v8::Local<v8::ArrayBufferView> view =
        v8::Local<v8::ArrayBufferView>::Cast(value);

    backing_store = view->Buffer()->GetBackingStore();
    offset = view->ByteOffset();
    size = view->ByteLength();
  } else if (value->IsSharedArrayBuffer()) {
    backing_store =
        v8::Local<v8::SharedArrayBuffer>::Cast(value)->GetBackingStore();
    size = backing_store->ByteLength();
  } else {
    backing_store = v8::Local<v8::ArrayBuffer>::Cast(value)->GetBackingStore();
    size = backing_store->ByteLength();
  }

  {
    const std::lock_guard<std::mutex> lock(backing_stores_mutex_);
    backing_stores_[bv_ptr] = backing_store;
  }

  bv_ptr->type = value->IsSharedArrayBuffer() ? type_shared_array_buffer
                                              : type_array_buffer;
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  bv_ptr->backing_store_ptr =
      static_cast<char*>(backing_store->Data()) + offset;
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  bv_ptr->len = size;
}

void BinaryValueFactory::DeleteBackingStoreRef(BinaryValue* bv_ptr) {
  const std::lock_guard<std::mutex> lock(backing_stores_mutex_);
  auto iter = backing_stores_.find(bv_ptr);
  if (iter == backing_stores_.end()) {
    return;
  }

  auto backing_store = std::move(iter->second);
  backing_stores_.erase(iter);

  // See note in BinaryValueFactory::DeletePersistentHandle. Same applies here.
  isolate_manager_->Run([ptr = std::move(backing_store)](
                            v8::Isolate* /*isolate*/) mutable { ptr.reset(); });
}

auto BinaryValueFactory::FromString(std::string str,
                                    BinaryTypes type) -> BinaryValue::Ptr {
  BinaryValue::Ptr res = New();
  res->len = str.size();
  res->type = type;
  res->bytes = new char[res->len + 1];
  std::copy(str.begin(), str.end(), res->bytes);
  res->bytes[res->len] = '\0';
  return res;
}

auto BinaryValueFactory::ToValue(v8::Local<v8::Context> context,
                                 BinaryValue* bv_ptr) -> v8::Local<v8::Value> {
  v8::Isolate* isolate = context->GetIsolate();

  if (bv_ptr->type == type_null) {
    return v8::Null(isolate);
  }

  if (bv_ptr->type == type_undefined) {
    return v8::Undefined(isolate);
  }

  if (bv_ptr->type == type_integer) {
    return v8::Integer::New(isolate, static_cast<int32_t>(bv_ptr->int_val));
  }

  if (bv_ptr->type == type_double) {
    return v8::Number::New(isolate, bv_ptr->double_val);
  }

  if (bv_ptr->type == type_bool) {
    return v8::Boolean::New(isolate, bv_ptr->int_val != 0);
  }

  if (bv_ptr->type == type_function || bv_ptr->type == type_symbol ||
      bv_ptr->type == type_promise || bv_ptr->type == type_array ||
      bv_ptr->type == type_object) {
    return GetPersistentHandle(isolate, bv_ptr);
  }

  if (bv_ptr->type == type_date) {
    return v8::Date::New(context, bv_ptr->double_val).ToLocalChecked();
  }

  if (bv_ptr->type == type_str_utf8) {
    return v8::String::NewFromUtf8(isolate, bv_ptr->bytes,
                                   v8::NewStringType::kNormal,
                                   static_cast<int>(bv_ptr->len))
        .ToLocalChecked();
  }

  // Unknown type!
  // Note: we skip shared array buffers, so for now at least, handles to shared
  // array buffers can only be transmitted from JS to Python.
  return v8::Undefined(isolate);
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

// From v8/src/d8.cc:
auto BinaryValueFactory::FromExceptionMessage(
    v8::Local<v8::Context> context,
    v8::Local<v8::Message> message,
    v8::Local<v8::Value> exception_obj,
    BinaryTypes result_type) -> BinaryValue::Ptr {
  std::stringstream msg;

  // Converts a V8 value to a C string.
  auto ToCString = [](const v8::String::Utf8Value& value) {
    return (*value == nullptr) ? "<string conversion failed>" : *value;
  };

  const v8::String::Utf8Value exception(context->GetIsolate(), exception_obj);
  const char* exception_string = ToCString(exception);
  if (message.IsEmpty()) {
    // V8 didn't provide any extra information about this error; just
    // print the exception.
    msg << exception_string << "\n";
  } else if (message->GetScriptOrigin().Options().IsWasm()) {
    // Print wasm-function[(function index)]:(offset): (message).
    const int function_index = message->GetWasmFunctionIndex();
    const int offset = message->GetStartColumn(context).FromJust();
    msg << "wasm-function[" << function_index << "]:0x" << std::hex << offset
        << std::dec << ": " << exception_string << "\n";
  } else {
    // Print (filename):(line number): (message).
    const v8::String::Utf8Value filename(
        context->GetIsolate(), message->GetScriptOrigin().ResourceName());
    const char* filename_string = ToCString(filename);
    const int linenum = message->GetLineNumber(context).FromMaybe(-1);
    msg << filename_string << ":" << linenum << ": " << exception_string
        << "\n";
    v8::Local<v8::String> sourceline;
    if (message->GetSourceLine(context).ToLocal(&sourceline)) {
      // Print line of source code.
      const v8::String::Utf8Value sourcelinevalue(context->GetIsolate(),
                                                  sourceline);
      const char* sourceline_string = ToCString(sourcelinevalue);
      msg << sourceline_string << "\n";
      // Print wavy underline (GetUnderline is deprecated).
      const int start = message->GetStartColumn();
      const int end = std::max(message->GetEndColumn(), start + 1);
      for (int i = 0; i < start; i++) {
        msg << " ";
      }
      for (int i = start; i < end; i++) {
        msg << "^";
      }
      msg << "\n";
    }
  }
  v8::Local<v8::Value> stack_trace_string;
  if (v8::TryCatch::StackTrace(context, exception_obj)
          .ToLocal(&stack_trace_string) &&
      stack_trace_string->IsString()) {
    const v8::String::Utf8Value stack_trace(
        context->GetIsolate(), stack_trace_string.As<v8::String>());
    msg << "\n";
    msg << ToCString(stack_trace);
    msg << "\n";
  }
  return FromString(msg.str(), result_type);
}

}  // namespace MiniRacer
