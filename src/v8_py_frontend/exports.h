#ifndef INCLUDE_MINI_RACER_EXPORTS_H
#define INCLUDE_MINI_RACER_EXPORTS_H

#include <cstddef>
#include <cstdint>
#include "binary_value.h"
#include "callback.h"

#ifdef V8_OS_WIN
#define LIB_EXPORT __declspec(dllexport)
#else  // V8_OS_WIN
#define LIB_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

// This lint check wants us to make classes to encompass parameters, which
// isn't very helpful in a low-level cross-language API (we'd be just as
// likely, if not more likely, to mess up the Python representation of any
// struct created to encompass these parameters):
// NOLINTBEGIN(bugprone-easily-swappable-parameters)

/** Initialize V8. Can be called at most once per process. */
LIB_EXPORT void mr_init_v8(const char* v8_flags,
                           const char* icu_path,
                           const char* snapshot_path);

/** Determine the V8 version. **/
LIB_EXPORT auto mr_v8_version() -> char const*;

/** Determine whether V8 is using the security sandbox at runtime. **/
LIB_EXPORT auto mr_v8_is_using_sandbox() -> bool;

/** Initialize a MiniRacer context.
 *
 * A MiniRacer context is an isolated JavaScript execution envrionment. It
 * contains one v8::Isolate, one v8::Context, one message loop thread, and
 * one pool of BinaryValueHandles and asynchronous tasks.
 *
 * The given callback function pointer must point to valid memory for the
 * the entire lifetime of this context (assuming any async tasks are started
 * or mr_get_js_callback is used).
 *
 * The callback will be called from within the Isolate message loop, while
 * holding the isolate lock. It should thus return as soon as possible, and
 * not attempt to make new calls into V8 (as they would deadlock).
 * Consequently the best thing for the callback to do is to signal another
 * thread (e.g., using a future or thread-safe queue) and immediately return.
 **/
LIB_EXPORT auto mr_init_context(MiniRacer::Callback callback) -> uint64_t;

/** Free a MiniRacer context.
 *
 * This shuts down the v8::ISolate, v8::Context, the message loop thread, and
 * frees any remaining BinaryValueHandles and asynchronous task handles.
 **/
LIB_EXPORT void mr_free_context(uint64_t context_id);

/** Count the number of living MiniRacer context objects.
 *
 * This function is intended for use in debugging only.
 **/
LIB_EXPORT auto mr_context_count() -> size_t;

/** Configure the V8 hard memory limit. **/
LIB_EXPORT void mr_set_hard_memory_limit(uint64_t context_id, size_t limit);

/** Configure the V8 soft memory limit. **/
LIB_EXPORT void mr_set_soft_memory_limit(uint64_t context_id, size_t limit);

/** Determine whether V8 reached the configured hard memory limit. **/
LIB_EXPORT auto mr_hard_memory_limit_reached(uint64_t context_id) -> bool;

/** Determine whether V8 reached the configured soft memory limit. **/
LIB_EXPORT auto mr_soft_memory_limit_reached(uint64_t context_id) -> bool;

/** Signal to V8 that the system is low on memory.
 *
 * This makes V8 more aggressively collect and free unused objects.
 **/
LIB_EXPORT void mr_low_memory_notification(uint64_t context_id);

/** Make a JS callback wrapping the C callback supplied to mr_init_context.
 *
 * When the given JS function is called, any args will be packed into an array
 * and passed to the C callback.
 **/
LIB_EXPORT auto mr_make_js_callback(uint64_t context_id, uint64_t callback_id)
    -> MiniRacer::BinaryValueHandle*;

/** Allocate a BinaryValueHandle containing the given int or int-like data.
 *
 * If used as an argument in another call, this value will be rendered into
 * JavaScript as boolean, number, undefined, or null, depending on the specified
 * type.
 **/
LIB_EXPORT auto mr_alloc_int_val(uint64_t context_id,
                                 int64_t val,
                                 MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle*;

/** Allocate a BinaryValueHandle containing the given double-precision number.
 *
 * If used as an argument in another call, this value will be rendered into
 * JavaScript as a number or Date, depending on the specified type.
 **/
LIB_EXPORT auto mr_alloc_double_val(uint64_t context_id,
                                    double val,
                                    MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle*;

/** Allocate a BinaryValueHandle pointing to a copy of the given utf-8 string.
 *
 * If used as an argument in another call, this value will be rendered into
 * JavaScript as an ordinary string. Only type type_str_utf8 is supported.
 **/
LIB_EXPORT auto mr_alloc_string_val(uint64_t context_id,
                                    char* val,
                                    uint64_t len,
                                    MiniRacer::BinaryTypes type)
    -> MiniRacer::BinaryValueHandle*;

/** Free the value pointed to by a BinaryValueHandle. */
LIB_EXPORT void mr_free_value(uint64_t context_id,
                              MiniRacer::BinaryValueHandle* val_handle);

/** Count the number of BinaryValueHandles which have been produced by the given
 *context and not freed yet.
 *
 * This function is intended for use in debugging only.
 **/
LIB_EXPORT auto mr_value_count(uint64_t context_id) -> size_t;

/** Get the V8 object identity hash for the given object. **/
LIB_EXPORT auto mr_get_identity_hash(uint64_t context_id,
                                     MiniRacer::BinaryValueHandle* obj_handle)
    -> MiniRacer::BinaryValueHandle*;

/** Call JavaScript `Object.getOwnPropertyNames()`.
 *
 * Returns an array of names, or an exception in case of error.
 **/
LIB_EXPORT auto mr_get_own_property_names(
    uint64_t context_id,
    MiniRacer::BinaryValueHandle* obj_handle) -> MiniRacer::BinaryValueHandle*;

/** Call JavaScript `obj[key]`.
 *
 * Returns the resulting value, or an exception in case of error.
 *
 * Returns an exception of type type_key_exception if the key cannot be
 * found.
 **/
LIB_EXPORT auto mr_get_object_item(uint64_t context_id,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle)
    -> MiniRacer::BinaryValueHandle*;

/** Call JavaScript `obj[key] = val`.
 *
 * Returns a MiniRacer::BinaryValueHandle* which is normally true except in
 * case of deletion failure, or an exception in case of error.
 *
 * Returns an exception of type type_key_exception if the key cannot be
 * found.
 **/
LIB_EXPORT auto mr_set_object_item(uint64_t context_id,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle,
                                   MiniRacer::BinaryValueHandle* val_handle)
    -> MiniRacer::BinaryValueHandle*;

/** Call JavaScript `delete obj[key]`.
 *
 * Returns a MiniRacer::BinaryValueHandle* which is normally true except in
 * case of deletion failure, or an exception in case of error.
 *
 * Returns an exception of type type_key_exception if the key cannot be
 * found.
 **/
LIB_EXPORT auto mr_del_object_item(uint64_t context_id,
                                   MiniRacer::BinaryValueHandle* obj_handle,
                                   MiniRacer::BinaryValueHandle* key_handle)
    -> MiniRacer::BinaryValueHandle*;

/** Call JavaScript `Array.prototype.splice(array, start, delete_count,
 * [new_val])`.
 *
 * Note that while Array.prototype.splice() accepts 0 to N new values, this
 * function only accepts either 0 (indicated by nullptr) or 1 new value.
 *
 * The result of the operation (passed into the callback) is either an Array
 * containing the deleted elements, or an exception in case of failure.
 **/
LIB_EXPORT auto mr_splice_array(uint64_t context_id,
                                MiniRacer::BinaryValueHandle* array_handle,
                                int32_t start,
                                int32_t delete_count,
                                MiniRacer::BinaryValueHandle* new_val_handle)
    -> MiniRacer::BinaryValueHandle*;

/** Cancel the given asynchronous task.
 *
 * (Such tasks are started by mr_eval, mr_call_function, mr_heap_stats, and
 * mr_heap_snapshot).
 **/
LIB_EXPORT void mr_cancel_task(uint64_t context_id, uint64_t task_id);

/** Evaluate the given JavaScript code.
 *
 * Code can be evaluated by, e.g., mr_alloc_string_val.
 *
 * This call is processed asynchronously and as such accepts a callback ID.
 * The callback ID and a MiniRacer::BinaryValueHandle* containing the
 * evaluation result are passed back to the callback upon completion. A task ID
 * is returned which can be passed back to mr_cancel_task to cancel evaluation.
 *
 * The result of the evaluation (usually the value of the last statement in the
 * code snippet) is supplied as the evaluation result, except in case of an
 * exception, wherein instead the exception is supplied as the evaluation
 * result.
 */
LIB_EXPORT auto mr_eval(uint64_t context_id,
                        MiniRacer::BinaryValueHandle* code_handle,
                        uint64_t callback_id) -> uint64_t;

/** Call JavaScript `func.call(this, ...argv)`.
 *
 * This call is processed asynchronously and as such accepts a callback ID.
 * The callback ID and a MiniRacer::BinaryValueHandle* containing the
 * evaluation result are passed back to the callback upon completion. A task ID
 * is returned which can be passed back to mr_cancel_task to cancel evaluation.
 **/
LIB_EXPORT auto mr_call_function(uint64_t context_id,
                                 MiniRacer::BinaryValueHandle* func_handle,
                                 MiniRacer::BinaryValueHandle* this_handle,
                                 MiniRacer::BinaryValueHandle* argv_handle,
                                 uint64_t callback_id) -> uint64_t;

/** Get stats for the V8 heap.
 *
 * This function is intended for use in debugging only.
 *
 * This call is processed asynchronously and as such accepts a callback ID.
 * The callback ID and a MiniRacer::BinaryValueHandle* containing the
 * evaluation result are passed back to the callback upon completion. A task ID
 * is returned which can be passed back to mr_cancel_task to cancel evaluation.
 **/
LIB_EXPORT auto mr_heap_stats(uint64_t context_id,
                              uint64_t callback_id) -> uint64_t;

/** Get a snapshot of the V8 heap.
 *
 * This function is intended for use in debugging only.
 *
 * This call is processed asynchronously and as such accepts a callback ID.
 * The callback ID and a MiniRacer::BinaryValueHandle* containing the
 * evaluation result are passed back to the callback upon completion. A task ID
 * is returned which can be passed back to mr_cancel_task to cancel evaluation.
 **/
LIB_EXPORT auto mr_heap_snapshot(uint64_t context_id,
                                 uint64_t callback_id) -> uint64_t;

// NOLINTEND(bugprone-easily-swappable-parameters)

}  // end extern "C"

#endif  // INCLUDE_MINI_RACER_EXPORTS_H
