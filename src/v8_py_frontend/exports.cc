#include <v8-version-string.h>
#include <cstddef>
#include <cstdint>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
#include "gsl_stub.h"
#include "mini_racer.h"

#ifdef V8_OS_WIN
#define LIB_EXPORT __declspec(dllexport)
#else  // V8_OS_WIN
#define LIB_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

LIB_EXPORT auto mr_eval(MiniRacer::Context* mr_context,
                        char* str,
                        uint64_t len,
                        MiniRacer::Callback callback,
                        uint64_t callback_id)
    -> MiniRacer::CancelableTaskHandle* {
  return mr_context->Eval(std::string(str, len), callback, callback_id)
      .release();
}

LIB_EXPORT void mr_init_v8(const char* v8_flags,
                           const char* icu_path,
                           const char* snapshot_path) {
  MiniRacer::init_v8(v8_flags, icu_path, snapshot_path);
}

LIB_EXPORT auto mr_init_context() -> gsl::owner<MiniRacer::Context*> {
  return new MiniRacer::Context();
}

LIB_EXPORT void mr_free_value(MiniRacer::Context* mr_context,
                              MiniRacer::BinaryValueHandle* val_handle) {
  mr_context->FreeBinaryValue(val_handle);
}

LIB_EXPORT auto mr_alloc_int_val(MiniRacer::Context* mr_context,
                                 int64_t val,
                                 MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->AllocBinaryValue(val, type);
}

LIB_EXPORT auto mr_alloc_double_val(MiniRacer::Context* mr_context,
                                    double val,
                                    MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->AllocBinaryValue(val, type);
}

LIB_EXPORT auto mr_alloc_string_val(MiniRacer::Context* mr_context,
                                    char* val,
                                    uint64_t len,
                                    MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->AllocBinaryValue(std::string_view(val, len), type);
}

LIB_EXPORT void mr_free_context(gsl::owner<MiniRacer::Context*> mr_context) {
  delete mr_context;
}

LIB_EXPORT void mr_free_task_handle(
    gsl::owner<MiniRacer::CancelableTaskHandle*> task_handle) {
  delete task_handle;
}

LIB_EXPORT auto mr_heap_stats(MiniRacer::Context* mr_context,
                              MiniRacer::Callback callback,
                              uint64_t callback_id)
    -> MiniRacer::CancelableTaskHandle* {
  return mr_context->HeapStats(callback, callback_id).release();
}

LIB_EXPORT void mr_set_hard_memory_limit(MiniRacer::Context* mr_context,
                                         size_t limit) {
  mr_context->SetHardMemoryLimit(limit);
}

LIB_EXPORT void mr_set_soft_memory_limit(MiniRacer::Context* mr_context,
                                         size_t limit) {
  mr_context->SetSoftMemoryLimit(limit);
}

LIB_EXPORT auto mr_hard_memory_limit_reached(MiniRacer::Context* mr_context)
    -> bool {
  return mr_context->IsHardMemoryLimitReached();
}

LIB_EXPORT auto mr_soft_memory_limit_reached(MiniRacer::Context* mr_context)
    -> bool {
  return mr_context->IsSoftMemoryLimitReached();
}

LIB_EXPORT void mr_low_memory_notification(MiniRacer::Context* mr_context) {
  mr_context->ApplyLowMemoryNotification();
}

LIB_EXPORT auto mr_v8_version() -> char const* {
  return V8_VERSION_STRING;
}

LIB_EXPORT void mr_attach_promise_then(
    MiniRacer::Context* mr_context,
    MiniRacer::BinaryValueHandle* promise_handle,
    MiniRacer::Callback callback,
    uint64_t callback_id) {
  mr_context->AttachPromiseThen(promise_handle, callback, callback_id);
}

LIB_EXPORT auto mr_get_identity_hash(MiniRacer::Context* mr_context,
                                     MiniRacer::BinaryValueHandle* obj_handle)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->GetIdentityHash(obj_handle);
}

LIB_EXPORT auto mr_get_own_property_names(
    MiniRacer::Context* mr_context,
    MiniRacer::BinaryValueHandle* obj_handle) -> MiniRacer::BinaryValueHandle* {
  return mr_context->GetOwnPropertyNames(obj_handle);
}

LIB_EXPORT auto mr_get_object_item(MiniRacer::Context* mr_context,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->GetObjectItem(obj_handle, key_handle);
}

LIB_EXPORT auto mr_set_object_item(MiniRacer::Context* mr_context,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle,
                                   MiniRacer::BinaryValueHandle* val_handle)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->SetObjectItem(obj_handle, key_handle, val_handle);
}

LIB_EXPORT auto mr_del_object_item(MiniRacer::Context* mr_context,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->DelObjectItem(obj_handle, key_handle);
}

LIB_EXPORT auto mr_splice_array(MiniRacer::Context* mr_context,
                                MiniRacer::BinaryValueHandle* array_handle,
                                int32_t start,
                                int32_t delete_count,
                                MiniRacer::BinaryValueHandle* new_val_handle)
    -> MiniRacer::BinaryValueHandle* {
  return mr_context->SpliceArray(array_handle, start, delete_count,
                                 new_val_handle);
}

LIB_EXPORT auto mr_call_function(MiniRacer::Context* mr_context,
                                 MiniRacer::BinaryValueHandle* func_handle,
                                 MiniRacer::BinaryValueHandle* this_handle,
                                 MiniRacer::BinaryValueHandle* argv_handle,
                                 MiniRacer::Callback callback,
                                 uint64_t callback_id)
    -> MiniRacer::CancelableTaskHandle* {
  return mr_context
      ->CallFunction(func_handle, this_handle, argv_handle, callback,
                     callback_id)
      .release();
}

// FOR DEBUGGING ONLY
LIB_EXPORT auto mr_heap_snapshot(MiniRacer::Context* mr_context,
                                 MiniRacer::Callback callback,
                                 uint64_t callback_id)
    -> MiniRacer::CancelableTaskHandle* {
  return mr_context->HeapSnapshot(callback, callback_id).release();
}

LIB_EXPORT auto mr_value_count(MiniRacer::Context* mr_context) -> size_t {
  return mr_context->BinaryValueCount();
}

}  // end extern "C"
