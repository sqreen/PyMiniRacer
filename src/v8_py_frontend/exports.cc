#include "exports.h"
#include <v8-initialization.h>
#include <v8-version-string.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include "binary_value.h"
#include "callback.h"
#include "context.h"
#include "context_factory.h"

namespace {
auto GetContext(uint64_t context_id) -> std::shared_ptr<MiniRacer::Context> {
  auto* context_factory = MiniRacer::ContextFactory::Get();
  if (context_factory == nullptr) {
    return nullptr;
  }
  return context_factory->GetContext(context_id);
}
}  // end anonymous namespace

// This lint check wants us to make classes to encompass parameters, which
// isn't very helpful in a low-level cross-language API (we'd be just as
// likely, if not more likely, to mess up the Python representation of any
// struct created to encompass these parameters):
// NOLINTBEGIN(bugprone-easily-swappable-parameters)

LIB_EXPORT auto mr_eval(uint64_t context_id,
                        MiniRacer::BinaryValueHandle* code_handle,
                        uint64_t callback_id) -> uint64_t {
  auto context = GetContext(context_id);
  if (!context) {
    return 0;
  }
  return context->Eval(code_handle, callback_id);
}

LIB_EXPORT void mr_init_v8(const char* v8_flags,
                           const char* icu_path,
                           const char* snapshot_path) {
  MiniRacer::ContextFactory::Init(v8_flags, icu_path, snapshot_path);
}

LIB_EXPORT auto mr_init_context(MiniRacer::Callback callback) -> uint64_t {
  auto* context_factory = MiniRacer::ContextFactory::Get();
  if (context_factory == nullptr) {
    return 0;
  }
  return context_factory->MakeContext(callback);
}

LIB_EXPORT void mr_free_context(uint64_t context_id) {
  auto* context_factory = MiniRacer::ContextFactory::Get();
  if (context_factory == nullptr) {
    return;
  }
  context_factory->FreeContext(context_id);
}

LIB_EXPORT auto mr_context_count() -> size_t {
  auto* context_factory = MiniRacer::ContextFactory::Get();
  if (context_factory == nullptr) {
    return std::numeric_limits<size_t>::max();
  }
  return context_factory->Count();
}

LIB_EXPORT void mr_free_value(uint64_t context_id,
                              MiniRacer::BinaryValueHandle* val_handle) {
  auto context = GetContext(context_id);
  if (!context) {
    return;
  }
  context->FreeBinaryValue(val_handle);
}

LIB_EXPORT auto mr_alloc_int_val(uint64_t context_id,
                                 int64_t val,
                                 MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->AllocBinaryValue(val, type);
}

LIB_EXPORT auto mr_alloc_double_val(uint64_t context_id,
                                    double val,
                                    MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->AllocBinaryValue(val, type);
}

LIB_EXPORT auto mr_alloc_string_val(uint64_t context_id,
                                    char* val,
                                    uint64_t len,
                                    MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->AllocBinaryValue(std::string_view(val, len), type);
}

LIB_EXPORT void mr_cancel_task(uint64_t context_id, uint64_t task_id) {
  auto context = GetContext(context_id);
  if (!context) {
    return;
  }
  context->CancelTask(task_id);
}

LIB_EXPORT auto mr_heap_stats(uint64_t context_id,
                              uint64_t callback_id) -> uint64_t {
  auto context = GetContext(context_id);
  if (!context) {
    return 0;
  }
  return context->HeapStats(callback_id);
}

LIB_EXPORT void mr_set_hard_memory_limit(uint64_t context_id, size_t limit) {
  auto context = GetContext(context_id);
  if (!context) {
    return;
  }
  context->SetHardMemoryLimit(limit);
}

LIB_EXPORT void mr_set_soft_memory_limit(uint64_t context_id, size_t limit) {
  auto context = GetContext(context_id);
  if (!context) {
    return;
  }
  context->SetSoftMemoryLimit(limit);
}

LIB_EXPORT auto mr_hard_memory_limit_reached(uint64_t context_id) -> bool {
  auto context = GetContext(context_id);
  if (!context) {
    return false;
  }
  return context->IsHardMemoryLimitReached();
}

LIB_EXPORT auto mr_soft_memory_limit_reached(uint64_t context_id) -> bool {
  auto context = GetContext(context_id);
  if (!context) {
    return false;
  }
  return context->IsSoftMemoryLimitReached();
}

LIB_EXPORT void mr_low_memory_notification(uint64_t context_id) {
  auto context = GetContext(context_id);
  if (!context) {
    return;
  }
  context->ApplyLowMemoryNotification();
}

LIB_EXPORT auto mr_make_js_callback(uint64_t context_id, uint64_t callback_id)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->MakeJSCallback(callback_id);
}

LIB_EXPORT auto mr_v8_version() -> char const* {
  return V8_VERSION_STRING;
}

LIB_EXPORT auto mr_v8_is_using_sandbox() -> bool {
  return v8::V8::IsSandboxConfiguredSecurely();
}

LIB_EXPORT auto mr_get_identity_hash(uint64_t context_id,
                                     MiniRacer::BinaryValueHandle* obj_handle)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->GetIdentityHash(obj_handle);
}

LIB_EXPORT auto mr_get_own_property_names(
    uint64_t context_id,
    MiniRacer::BinaryValueHandle* obj_handle) -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->GetOwnPropertyNames(obj_handle);
}

LIB_EXPORT auto mr_get_object_item(uint64_t context_id,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->GetObjectItem(obj_handle, key_handle);
}

LIB_EXPORT auto mr_set_object_item(uint64_t context_id,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle,
                                   MiniRacer::BinaryValueHandle* val_handle)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->SetObjectItem(obj_handle, key_handle, val_handle);
}

LIB_EXPORT auto mr_del_object_item(uint64_t context_id,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->DelObjectItem(obj_handle, key_handle);
}

LIB_EXPORT auto mr_splice_array(uint64_t context_id,
                                MiniRacer::BinaryValueHandle* array_handle,
                                int32_t start,
                                int32_t delete_count,
                                MiniRacer::BinaryValueHandle* new_val_handle)
    -> MiniRacer::BinaryValueHandle* {
  auto context = GetContext(context_id);
  if (!context) {
    return nullptr;
  }
  return context->SpliceArray(array_handle, start, delete_count,
                              new_val_handle);
}

LIB_EXPORT auto mr_call_function(uint64_t context_id,
                                 MiniRacer::BinaryValueHandle* func_handle,
                                 MiniRacer::BinaryValueHandle* this_handle,
                                 MiniRacer::BinaryValueHandle* argv_handle,
                                 uint64_t callback_id) -> uint64_t {
  auto context = GetContext(context_id);
  if (!context) {
    return 0;
  }
  return context->CallFunction(func_handle, this_handle, argv_handle,
                               callback_id);
}

LIB_EXPORT auto mr_heap_snapshot(uint64_t context_id,
                                 uint64_t callback_id) -> uint64_t {
  auto context = GetContext(context_id);
  if (!context) {
    return 0;
  }
  return context->HeapSnapshot(callback_id);
}

LIB_EXPORT auto mr_value_count(uint64_t context_id) -> size_t {
  auto context = GetContext(context_id);
  if (!context) {
    return 0;
  }
  return context->BinaryValueCount();
}

// NOLINTEND(bugprone-easily-swappable-parameters)
