#include "object_manipulator.h"
#include <v8-container.h>
#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-object.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <string>
#include "binary_value.h"

namespace MiniRacer {

ObjectManipulator::ObjectManipulator(v8::Persistent<v8::Context>* context,
                                     BinaryValueFactory* bv_factory)
    : context_(context), bv_factory_(bv_factory) {}

auto ObjectManipulator::GetIdentityHash(v8::Isolate* isolate,
                                        v8::Persistent<v8::Value>* object)
    -> int {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object =
      object->Get(isolate).As<v8::Object>();

  return local_object->GetIdentityHash();
}

auto ObjectManipulator::GetOwnPropertyNames(
    v8::Isolate* isolate,
    v8::Persistent<v8::Value>* object) const -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object =
      object->Get(isolate).As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Array> names =
      local_object->GetPropertyNames(local_context).ToLocalChecked();

  return bv_factory_->FromValue(local_context, names);
}

auto ObjectManipulator::KeyToValue(v8::Isolate* isolate, const std::string& key)
    -> v8::Local<v8::Value> {
  return v8::String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked();
}

auto ObjectManipulator::KeyToValue(v8::Isolate* isolate,
                                   double key) -> v8::Local<v8::Value> {
  return v8::Number::New(isolate, key);
}

}  // end namespace MiniRacer
