#include <v8-version-string.h>
#include "mini_racer.h"

#ifdef V8_OS_WIN
#define LIB_EXPORT __declspec(dllexport)
#else  // V8_OS_WIN
#define LIB_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

LIB_EXPORT MiniRacer::BinaryValue* mr_eval_context(
    MiniRacer::Context* mr_context,
    char* str,
    int len,
    unsigned long timeout) {
  return mr_context->Eval(std::string(str, len), timeout).release();
}

LIB_EXPORT void mr_init_v8(const char* v8_flags,
                           const char* icu_path,
                           const char* snapshot_path) {
  MiniRacer::init_v8(v8_flags, icu_path, snapshot_path);
}

LIB_EXPORT MiniRacer::Context* mr_init_context() {
  return new MiniRacer::Context();
}

LIB_EXPORT void mr_free_value(MiniRacer::Context* mr_context,
                              MiniRacer::BinaryValue* val) {
  mr_context->FreeBinaryValue(val);
}

LIB_EXPORT void mr_free_context(MiniRacer::Context* mr_context) {
  delete mr_context;
}

LIB_EXPORT MiniRacer::BinaryValue* mr_heap_stats(
    MiniRacer::Context* mr_context) {
  return mr_context->HeapStats().release();
}

LIB_EXPORT void mr_set_hard_memory_limit(MiniRacer::Context* mr_context,
                                         size_t limit) {
  mr_context->SetHardMemoryLimit(limit);
}

LIB_EXPORT void mr_set_soft_memory_limit(MiniRacer::Context* mr_context,
                                         size_t limit) {
  mr_context->SetSoftMemoryLimit(limit);
}

LIB_EXPORT bool mr_soft_memory_limit_reached(MiniRacer::Context* mr_context) {
  return mr_context->IsSoftMemoryLimitReached();
}

LIB_EXPORT void mr_low_memory_notification(MiniRacer::Context* mr_context) {
  mr_context->ApplyLowMemoryNotification();
}

LIB_EXPORT char const* mr_v8_version() {
  return V8_VERSION_STRING;
}

// FOR DEBUGGING ONLY
LIB_EXPORT MiniRacer::BinaryValue* mr_heap_snapshot(
    MiniRacer::Context* mr_context) {
  return mr_context->HeapSnapshot().release();
}
}  // end extern "C"
