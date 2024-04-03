#ifndef INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H
#define INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H

#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-persistent-handle.h>
#include <v8-value.h>
#include <cstdint>
#include "binary_value.h"

namespace MiniRacer {

/** Manipulates v8::Object attributes, exposing APIs reachable from C (through
 * the MiniRacer::Context). */
class ObjectManipulator {
 public:
  ObjectManipulator(v8::Persistent<v8::Context>* context,
                    BinaryValueFactory* bv_factory);

  static auto GetIdentityHash(v8::Isolate* isolate,
                              v8::Local<v8::Value> object) -> int;
  auto GetOwnPropertyNames(v8::Isolate* isolate, v8::Local<v8::Value> object)
      const -> BinaryValue::Ptr;
  auto Get(v8::Isolate* isolate,
           v8::Local<v8::Value> object,
           BinaryValue* key) -> BinaryValue::Ptr;
  void Set(v8::Isolate* isolate,
           v8::Local<v8::Value> object,
           BinaryValue* key,
           BinaryValue* val);
  auto Del(v8::Isolate* isolate,
           v8::Local<v8::Value> object,
           BinaryValue* key) -> bool;
  auto Splice(v8::Isolate* isolate,
              v8::Local<v8::Value> object,
              int32_t start,
              int32_t delete_count,
              BinaryValue* new_val) -> BinaryValue::Ptr;
  auto Call(v8::Isolate* isolate,
            v8::Local<v8::Value> func,
            BinaryValue* this_ptr,
            BinaryValue* argv_ptr) -> BinaryValue::Ptr;

 private:
  v8::Persistent<v8::Context>* context_;
  BinaryValueFactory* bv_factory_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H
