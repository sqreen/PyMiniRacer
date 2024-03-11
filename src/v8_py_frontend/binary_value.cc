#include "binary_value.h"

namespace MiniRacer {

void BinaryValueFactory::Free(BinaryValue* v) {
  if (!v) {
    return;
  }
  switch (v->type) {
    case type_execute_exception:
    case type_parse_exception:
    case type_oom_exception:
    case type_timeout_exception:
    case type_terminated_exception:
    case type_str_utf8:
      delete[] v->bytes;
      break;
    case type_bool:
    case type_double:
    case type_date:
    case type_null:
    case type_integer:
    case type_function:  // no value implemented
    case type_symbol:
    case type_object:
    case type_invalid:
      // the other types are scalar values
      break;
    case type_shared_array_buffer:
    case type_array_buffer:
      backing_stores_.erase(v);
      break;
  }
  delete v;
}

BinaryValue::Ptr BinaryValueFactory::ConvertFromV8(
    v8::Local<v8::Context> context,
    v8::Local<v8::Value> value) {
  BinaryValue::Ptr res = New();

  if (value->IsNull() || value->IsUndefined()) {
    res->type = type_null;
  } else if (value->IsInt32()) {
    res->type = type_integer;
    auto val = value->Uint32Value(context).ToChecked();
    res->int_val = val;
  }
  // ECMA-262, 4.3.20
  // http://www.ecma-international.org/ecma-262/5.1/#sec-4.3.19
  else if (value->IsNumber()) {
    res->type = type_double;
    double val = value->NumberValue(context).ToChecked();
    res->double_val = val;
  } else if (value->IsBoolean()) {
    res->type = type_bool;
    res->int_val = (value->IsTrue() ? 1 : 0);
  } else if (value->IsFunction()) {
    res->type = type_function;
  } else if (value->IsSymbol()) {
    res->type = type_symbol;
  } else if (value->IsDate()) {
    res->type = type_date;
    v8::Local<v8::Date> date = v8::Local<v8::Date>::Cast(value);

    double timestamp = date->ValueOf();
    res->double_val = timestamp;
  } else if (value->IsString()) {
    v8::Local<v8::String> rstr = value->ToString(context).ToLocalChecked();

    res->type = type_str_utf8;
    res->len = size_t(rstr->Utf8Length(context->GetIsolate()));  // in bytes
    size_t capacity = res->len + 1;
    res->bytes = new char[capacity];
    rstr->WriteUtf8(context->GetIsolate(), res->bytes);
  } else if (value->IsSharedArrayBuffer() || value->IsArrayBuffer() ||
             value->IsArrayBufferView()) {
    std::shared_ptr<v8::BackingStore> backing_store;
    size_t offset = 0;
    size_t size = 0;

    if (value->IsArrayBufferView()) {
      v8::Local<v8::ArrayBufferView> view =
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
    res->ptr_val = static_cast<char*>(backing_store->Data()) + offset;
    res->len = size;

  } else if (value->IsObject()) {
    res->type = type_object;
    res->int_val = value->ToObject(context).ToLocalChecked()->GetIdentityHash();
  } else {
    return BinaryValue::Ptr();
  }
  return res;
}

}  // namespace MiniRacer
