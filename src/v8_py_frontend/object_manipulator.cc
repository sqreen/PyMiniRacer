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
#include "context_holder.h"

namespace MiniRacer {

ObjectManipulator::ObjectManipulator(ContextHolder* context,
                                     BinaryValueFactory* bv_factory)
    : context_(context), bv_factory_(bv_factory) {}

auto ObjectManipulator::GetIdentityHash(v8::Isolate* isolate,
                                        BinaryValue* obj_ptr)
    -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_obj_val = obj_ptr->ToValue(local_context);
  const v8::Local<v8::Object> local_obj = local_obj_val.As<v8::Object>();

  return bv_factory_->New(static_cast<int64_t>(local_obj->GetIdentityHash()),
                          type_integer);
}

auto ObjectManipulator::GetOwnPropertyNames(v8::Isolate* isolate,
                                            BinaryValue* obj_ptr) const
    -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_obj_val = obj_ptr->ToValue(local_context);
  const v8::Local<v8::Object> local_obj = local_obj_val.As<v8::Object>();

  const v8::Local<v8::Array> names =
      local_obj->GetPropertyNames(local_context).ToLocalChecked();

  return bv_factory_->New(local_context, names);
}

auto ObjectManipulator::Get(v8::Isolate* isolate,
                            BinaryValue* obj_ptr,
                            BinaryValue* key_ptr) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_obj_val = obj_ptr->ToValue(local_context);
  const v8::Local<v8::Object> local_obj = local_obj_val.As<v8::Object>();
  const v8::Local<v8::Value> local_key = key_ptr->ToValue(local_context);

  if (!local_obj->Has(local_context, local_key).ToChecked()) {
    return bv_factory_->New("No such key", type_key_exception);
  }

  const v8::Local<v8::Value> value =
      local_obj->Get(local_context, local_key).ToLocalChecked();

  return bv_factory_->New(local_context, value);
}

auto ObjectManipulator::Set(v8::Isolate* isolate,
                            BinaryValue* obj_ptr,
                            BinaryValue* key_ptr,
                            BinaryValue* val_ptr) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_obj_val = obj_ptr->ToValue(local_context);
  const v8::Local<v8::Object> local_obj = local_obj_val.As<v8::Object>();
  const v8::Local<v8::Value> local_key = key_ptr->ToValue(local_context);
  const v8::Local<v8::Value> local_value = val_ptr->ToValue(local_context);

  local_obj->Set(local_context, local_key, local_value).ToChecked();

  return bv_factory_->New(true);
}

auto ObjectManipulator::Del(v8::Isolate* isolate,
                            BinaryValue* obj_ptr,
                            BinaryValue* key_ptr) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_obj_val = obj_ptr->ToValue(local_context);
  const v8::Local<v8::Object> local_obj = local_obj_val.As<v8::Object>();
  const v8::Local<v8::Value> local_key = key_ptr->ToValue(local_context);

  if (!local_obj->Has(local_context, local_key).ToChecked()) {
    return bv_factory_->New("No such key", type_key_exception);
  }

  return bv_factory_->New(
      local_obj->Delete(local_context, local_key).ToChecked());
}

auto ObjectManipulator::Splice(v8::Isolate* isolate,
                               BinaryValue* obj_ptr,
                               int32_t start,
                               int32_t delete_count,
                               BinaryValue* new_val_ptr) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_obj_val = obj_ptr->ToValue(local_context);
  const v8::Local<v8::Object> local_obj = local_obj_val.As<v8::Object>();

  // Array.prototype.splice doesn't exist in C++ in V8. We have to find the JS
  // function and call it:
  const v8::Local<v8::String> splice_name =
      v8::String::NewFromUtf8Literal(isolate, "splice");

  v8::Local<v8::Value> splice_val;
  if (!local_obj->Get(local_context, splice_name).ToLocal(&splice_val)) {
    return bv_factory_->New("no splice method on object",
                            type_execute_exception);
  }

  if (!splice_val->IsFunction()) {
    return bv_factory_->New("splice method is not a function",
                            type_execute_exception);
  }

  const v8::Local<v8::Function> splice_func = splice_val.As<v8::Function>();

  const v8::TryCatch trycatch(isolate);

  std::vector<v8::Local<v8::Value>> argv = {
      v8::Int32::New(isolate, start),
      v8::Int32::New(isolate, delete_count),
  };
  if (new_val_ptr != nullptr) {
    argv.push_back(new_val_ptr->ToValue(local_context));
  }

  v8::MaybeLocal<v8::Value> maybe_value = splice_func->Call(
      local_context, local_obj, static_cast<int>(argv.size()), argv.data());
  if (maybe_value.IsEmpty()) {
    return bv_factory_->New(local_context, trycatch.Message(),
                            trycatch.Exception(), type_execute_exception);
  }

  return bv_factory_->New(local_context, maybe_value.ToLocalChecked());
}

auto ObjectManipulator::Call(v8::Isolate* isolate,
                             BinaryValue* func_ptr,
                             BinaryValue* this_ptr,
                             BinaryValue* argv_ptr) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_func_val = func_ptr->ToValue(local_context);

  if (!local_func_val->IsFunction()) {
    return bv_factory_->New("function is not callable", type_execute_exception);
  }

  const v8::Local<v8::Function> local_func = local_func_val.As<v8::Function>();

  v8::Local<v8::Value> local_this;
  if (this_ptr == nullptr) {
    local_this = v8::Undefined(isolate);
  } else {
    local_this = this_ptr->ToValue(local_context);
  }

  const v8::Local<v8::Value> local_argv_value =
      argv_ptr->ToValue(local_context);

  if (!local_argv_value->IsArray()) {
    return bv_factory_->New("argv is not an array", type_execute_exception);
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
    return bv_factory_->New(local_context, trycatch.Message(),
                            trycatch.Exception(), type_execute_exception);
  }

  return bv_factory_->New(local_context, maybe_value.ToLocalChecked());
}

}  // end namespace MiniRacer
