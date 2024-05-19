#ifndef INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H
#define INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H

#include <v8-isolate.h>
#include <v8-persistent-handle.h>
#include <cstdint>
#include "binary_value.h"
#include "context_holder.h"

namespace MiniRacer {

/** Manipulates v8::Object attributes, exposing APIs reachable from C (through
 * the MiniRacer::Context).
 *
 * All methods in this function assume that the caller holds the Isolate lock
 * (i.e., is operating from the isolate message pump), and memory management of
 * the BinaryValue pointers is done by the caller. */
class ObjectManipulator {
 public:
  ObjectManipulator(ContextHolder* context, BinaryValueFactory* bv_factory);

  auto GetIdentityHash(v8::Isolate* isolate,
                       BinaryValue* obj_ptr) -> BinaryValue::Ptr;
  auto GetOwnPropertyNames(v8::Isolate* isolate,
                           BinaryValue* obj_ptr) const -> BinaryValue::Ptr;
  auto Get(v8::Isolate* isolate,
           BinaryValue* obj_ptr,
           BinaryValue* key_ptr) -> BinaryValue::Ptr;
  auto Set(v8::Isolate* isolate,
           BinaryValue* obj_ptr,
           BinaryValue* key_ptr,
           BinaryValue* val_ptr) -> BinaryValue::Ptr;
  auto Del(v8::Isolate* isolate,
           BinaryValue* obj_ptr,
           BinaryValue* key_ptr) -> BinaryValue::Ptr;
  auto Splice(v8::Isolate* isolate,
              BinaryValue* obj_ptr,
              int32_t start,
              int32_t delete_count,
              BinaryValue* new_val_ptr) -> BinaryValue::Ptr;
  auto Call(v8::Isolate* isolate,
            BinaryValue* func_ptr,
            BinaryValue* this_ptr,
            BinaryValue* argv_ptr) -> BinaryValue::Ptr;

 private:
  ContextHolder* context_;
  BinaryValueFactory* bv_factory_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_OBJECT_MANIPULATOR_H
