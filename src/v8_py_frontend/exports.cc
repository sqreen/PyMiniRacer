#include <v8-version-string.h>
#include <cstddef>
#include <cstdint>
#include "binary_value.h"
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
                        void* cb_data) -> MiniRacer::CancelableTaskHandle* {
  return mr_context->Eval(std::string(str, len), callback, cb_data).release();
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
                              gsl::owner<MiniRacer::BinaryValue*> val) {
  mr_context->FreeBinaryValue(val);
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
                              void* cb_data)
    -> MiniRacer::CancelableTaskHandle* {
  return mr_context->HeapStats(callback, cb_data).release();
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

// FOR DEBUGGING ONLY
LIB_EXPORT auto mr_heap_snapshot(MiniRacer::Context* mr_context,
                                 MiniRacer::Callback callback,
                                 void* cb_data)
    -> MiniRacer::CancelableTaskHandle* {
  return mr_context->HeapSnapshot(callback, cb_data).release();
}

LIB_EXPORT auto mr_full_eval_call_count(MiniRacer::Context* mr_context)
    -> uint64_t {
  return mr_context->FullEvalCallCount();
}

LIB_EXPORT auto mr_function_eval_call_count(MiniRacer::Context* mr_context)
    -> uint64_t {
  return mr_context->FunctionEvalCallCount();
}

}  // end extern "C"
