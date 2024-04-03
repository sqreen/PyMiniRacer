#include "object_manipulator.h"
#include <v8-container.h>
#include <v8-context.h>
#include <v8-exception.h>
#include <v8-function.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-object.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <cstdint>
#include <vector>
#include "binary_value.h"

namespace MiniRacer {

ObjectManipulator::ObjectManipulator(v8::Persistent<v8::Context>* context,
                                     BinaryValueFactory* bv_factory)
    : context_(context), bv_factory_(bv_factory) {}

auto ObjectManipulator::GetIdentityHash(v8::Isolate* isolate,
                                        v8::Local<v8::Value> object) -> int {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object = object.As<v8::Object>();

  return local_object->GetIdentityHash();
}

auto ObjectManipulator::GetOwnPropertyNames(v8::Isolate* isolate,
                                            v8::Local<v8::Value> object) const
    -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object = object.As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Array> names =
      local_object->GetPropertyNames(local_context).ToLocalChecked();

  return bv_factory_->FromValue(local_context, names);
}

auto ObjectManipulator::Get(v8::Isolate* isolate,
                            v8::Local<v8::Value> object,
                            BinaryValue* key) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object = object.As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);

  const v8::Local<v8::Value> local_key =
      bv_factory_->ToValue(local_context, key);

  if (!local_object->Has(local_context, local_key).ToChecked()) {
    // Return null if no item.
    return {};
  }

  const v8::Local<v8::Value> value =
      local_object->Get(local_context, local_key).ToLocalChecked();

  return bv_factory_->FromValue(local_context, value);
}

void ObjectManipulator::Set(v8::Isolate* isolate,
                            v8::Local<v8::Value> object,
                            BinaryValue* key,
                            BinaryValue* val) {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object = object.As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);

  const v8::Local<v8::Value> local_key =
      bv_factory_->ToValue(local_context, key);

  const v8::Local<v8::Value> local_value =
      bv_factory_->ToValue(local_context, val);

  local_object->Set(local_context, local_key, local_value).ToChecked();
}

auto ObjectManipulator::Del(v8::Isolate* isolate,
                            v8::Local<v8::Value> object,
                            BinaryValue* key) -> bool {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object = object.As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);

  const v8::Local<v8::Value> local_key =
      bv_factory_->ToValue(local_context, key);

  if (!local_object->Has(local_context, local_key).ToChecked()) {
    return false;
  }

  return local_object->Delete(local_context, local_key).ToChecked();
}

auto ObjectManipulator::Splice(v8::Isolate* isolate,
                               v8::Local<v8::Value> object,
                               int32_t start,
                               int32_t delete_count,
                               BinaryValue* new_val) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object = object.As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  // Array.prototype.splice doesn't exist in C++ in V8. We have to find the JS
  // function and call it:
  const v8::Local<v8::String> splice_name =
      v8::String::NewFromUtf8Literal(isolate, "splice");

  v8::Local<v8::Value> splice_val;
  if (!local_object->Get(local_context, splice_name).ToLocal(&splice_val)) {
    return bv_factory_->FromString("no splice method on object",
                                   type_execute_exception);
  }

  if (!splice_val->IsFunction()) {
    return bv_factory_->FromString("splice method is not a function",
                                   type_execute_exception);
  }

  const v8::Local<v8::Function> splice_func = splice_val.As<v8::Function>();

  const v8::TryCatch trycatch(isolate);

  std::vector<v8::Local<v8::Value>> argv = {
      v8::Int32::New(isolate, start),
      v8::Int32::New(isolate, delete_count),
  };
  if (new_val != nullptr) {
    argv.push_back(bv_factory_->ToValue(local_context, new_val));
  }

  v8::MaybeLocal<v8::Value> maybe_value = splice_func->Call(
      local_context, local_object, static_cast<int>(argv.size()), argv.data());
  if (maybe_value.IsEmpty()) {
    return bv_factory_->FromExceptionMessage(local_context, trycatch.Message(),
                                             trycatch.Exception(),
                                             type_execute_exception);
  }

  return bv_factory_->FromValue(local_context, maybe_value.ToLocalChecked());
}

auto ObjectManipulator::Call(v8::Isolate* isolate,
                             v8::Local<v8::Value> func,
                             BinaryValue* this_ptr,
                             BinaryValue* argv_ptr) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  if (!func->IsFunction()) {
    return bv_factory_->FromString("function is not callable",
                                   type_execute_exception);
  }

  const v8::Local<v8::Function> local_func = func.As<v8::Function>();

  const v8::Local<v8::Value> local_this =
      bv_factory_->ToValue(local_context, this_ptr);
  const v8::Local<v8::Value> local_argv_value =
      bv_factory_->ToValue(local_context, argv_ptr);

  if (!local_argv_value->IsArray()) {
    return bv_factory_->FromString("argv is not an array",
                                   type_execute_exception);
  }

  const v8::Local<v8::Array> local_argv_array =
      local_argv_value.As<v8::Array>();

  std::vector<v8::Local<v8::Value>> argv;
  for (uint32_t i = 0; i < local_argv_array->Length(); i++) {
    argv.push_back(local_argv_array->Get(local_context, i).ToLocalChecked());
  }

  const v8::TryCatch trycatch(isolate);

  v8::MaybeLocal<v8::Value> maybe_value = local_func->Call(
      local_context, local_this, static_cast<int>(argv.size()), argv.data());
  if (maybe_value.IsEmpty()) {
    return bv_factory_->FromExceptionMessage(local_context, trycatch.Message(),
                                             trycatch.Exception(),
                                             type_execute_exception);
  }

  return bv_factory_->FromValue(local_context, maybe_value.ToLocalChecked());
}

}  // end namespace MiniRacer
