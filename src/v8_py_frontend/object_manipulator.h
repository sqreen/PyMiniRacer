#ifndef INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H
#define INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H

#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-object.h>
#include <v8-persistent-handle.h>
#include <v8-value.h>
#include <string>
#include "binary_value.h"

namespace MiniRacer {

/** Manipulates v8::Object attributes, exposing APIs reachable from C (through
 * the MiniRacer::Context). */
class ObjectManipulator {
 public:
  ObjectManipulator(v8::Persistent<v8::Context>* context,
                    BinaryValueFactory* bv_factory);

  static auto GetIdentityHash(v8::Isolate* isolate,
                              v8::Persistent<v8::Value>* object) -> int;
  auto GetOwnPropertyNames(v8::Isolate* isolate,
                           v8::Persistent<v8::Value>* object) const
      -> BinaryValue::Ptr;
  template <typename Key>
  auto GetItem(v8::Isolate* isolate,
               v8::Persistent<v8::Value>* object,
               Key key) -> BinaryValue::Ptr;

 private:
  static auto KeyToValue(v8::Isolate* isolate,
                         const std::string& key) -> v8::Local<v8::Value>;

  static auto KeyToValue(v8::Isolate* isolate,
                         double key) -> v8::Local<v8::Value>;

  v8::Persistent<v8::Context>* context_;
  BinaryValueFactory* bv_factory_;
};

template <typename Key>
inline auto ObjectManipulator::GetItem(v8::Isolate* isolate,
                                       v8::Persistent<v8::Value>* object,
                                       Key key) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Object> local_object =
      object->Get(isolate).As<v8::Object>();
  const v8::Local<v8::Context> local_context = context_->Get(isolate);

  const v8::Local<v8::Value> local_key = KeyToValue(isolate, key);

  if (!local_object->Has(local_context, local_key).ToChecked()) {
    // Return null if no item.
    return {};
  }

  const v8::Local<v8::Value> value =
      local_object->Get(local_context, local_key).ToLocalChecked();

  return bv_factory_->FromValue(local_context, value);
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H
