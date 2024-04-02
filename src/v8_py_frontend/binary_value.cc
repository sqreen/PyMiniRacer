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
#include <sstream>
#include <string>
#include "gsl_stub.h"

namespace MiniRacer {

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

void BinaryValueFactory::Free(gsl::owner<BinaryValue*> val) {
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
      backing_stores_.erase(val);
      break;
    // We represent these types as pointers to a v8::Persistent:
    case type_symbol:
    case type_function:
    case type_object:
    case type_array:
    case type_promise:
      delete val->value_ptr;
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
    res->value_ptr =
        new v8::Persistent<v8::Value>(context->GetIsolate(), value);
    res->type = type_function;
  } else if (value->IsSymbol()) {
    res->value_ptr =
        new v8::Persistent<v8::Value>(context->GetIsolate(), value);
    res->type = type_symbol;
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
      backing_store =
          v8::Local<v8::ArrayBuffer>::Cast(value)->GetBackingStore();
      size = backing_store->ByteLength();
    }

    backing_stores_[res.get()] = backing_store;
    res->type = value->IsSharedArrayBuffer() ? type_shared_array_buffer
                                             : type_array_buffer;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    res->backing_store_ptr = static_cast<char*>(backing_store->Data()) + offset;
    res->len = size;

  } else if (value->IsPromise()) {
    res->type = type_promise;
    res->value_ptr =
        new v8::Persistent<v8::Value>(context->GetIsolate(), value);
  } else if (value->IsArray()) {
    res->type = type_array;
    res->value_ptr =
        new v8::Persistent<v8::Value>(context->GetIsolate(), value);
  } else if (value->IsObject()) {
    res->type = type_object;
    res->value_ptr =
        new v8::Persistent<v8::Value>(context->GetIsolate(), value);
  } else {
    return {};
  }
  return res;
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
                                 BinaryValue* ptr) -> v8::Local<v8::Value> {
  v8::Isolate* isolate = context->GetIsolate();

  if (ptr->type == type_null) {
    return v8::Null(isolate);
  }

  if (ptr->type == type_undefined) {
    return v8::Undefined(isolate);
  }

  if (ptr->type == type_integer) {
    return v8::Integer::New(isolate, static_cast<int32_t>(ptr->int_val));
  }

  if (ptr->type == type_double) {
    return v8::Number::New(isolate, ptr->double_val);
  }

  if (ptr->type == type_bool) {
    return v8::Boolean::New(isolate, ptr->int_val != 0);
  }

  if (ptr->type == type_function || ptr->type == type_symbol ||
      ptr->type == type_promise || ptr->type == type_array ||
      ptr->type == type_object) {
    return static_cast<v8::Persistent<v8::Value>*>(ptr->value_ptr)
        ->Get(isolate);
  }

  if (ptr->type == type_date) {
    return v8::Date::New(context, ptr->double_val).ToLocalChecked();
  }

  if (ptr->type == type_str_utf8) {
    return v8::String::NewFromUtf8(isolate, ptr->bytes).ToLocalChecked();
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
