#include "context.h"
#include <v8-initialization.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-persistent-handle.h>
#include <v8-platform.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
#include "object_manipulator.h"

namespace MiniRacer {

Context::Context(v8::Platform* platform)
    : isolate_manager_(platform),
      isolate_manager_stopper_(&isolate_manager_),
      isolate_memory_monitor_(&isolate_manager_),
      bv_factory_(&isolate_manager_),
      context_holder_(&isolate_manager_),
      code_evaluator_(context_holder_.Get(),
                      &bv_factory_,
                      &isolate_memory_monitor_),
      heap_reporter_(&bv_factory_),
      promise_attacher_(context_holder_.Get(), &bv_factory_),
      object_manipulator_(context_holder_.Get(), &bv_factory_),
      cancelable_task_runner_(&isolate_manager_),
      pending_task_waiter_(&pending_task_counter_) {}

template <typename Runnable>
auto Context::RunTask(Runnable runnable,
                      Callback callback,
                      uint64_t callback_id)
    -> std::unique_ptr<CancelableTaskHandle> {
  // Start an async task!

  // To make sure we perform an orderly exit, we track this async work, and
  // wait for it to complete before we start destructing the Context:
  pending_task_counter_.Increment();

  return cancelable_task_runner_.Schedule(
      /*runnable=*/
      std::move(runnable),
      /*on_completed=*/
      [callback, callback_id, this](const BinaryValue::Ptr& val) {
        pending_task_counter_.Decrement();
        callback(callback_id, val->GetHandle());
      },
      /*on_canceled=*/
      [callback, callback_id, this](const BinaryValue::Ptr& val) {
        if (val) {
          // Ignore the produced value, if any:
          bv_factory_.Free(val->GetHandle());
        }

        auto err =
            bv_factory_.New("execution terminated", type_terminated_exception);
        pending_task_counter_.Decrement();
        callback(callback_id, err->GetHandle());
      });
}

auto Context::Eval(const std::string& code,
                   Callback callback,
                   uint64_t callback_id)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [code, this](v8::Isolate* isolate) {
        return code_evaluator_.Eval(isolate, code);
      },
      callback, callback_id);
}

auto Context::HeapSnapshot(Callback callback, uint64_t callback_id)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [this](v8::Isolate* isolate) {
        return heap_reporter_.HeapSnapshot(isolate);
      },
      callback, callback_id);
}

auto Context::HeapStats(Callback callback, uint64_t callback_id)
    -> std::unique_ptr<CancelableTaskHandle> {
  return RunTask(
      [this](v8::Isolate* isolate) {
        return heap_reporter_.HeapStats(isolate);
      },
      callback, callback_id);
}

auto Context::AttachPromiseThen(BinaryValueHandle* promise_handle,
                                MiniRacer::Callback callback,
                                uint64_t callback_id) -> BinaryValueHandle* {
  auto promise_ptr = bv_factory_.FromHandle(promise_handle);
  if (!promise_ptr) {
    return bv_factory_.New("Bad handle: promise", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      .RunAndAwait(
          [this, promise_ptr, callback, callback_id](v8::Isolate* isolate) {
            return promise_attacher_.AttachPromiseThen(
                isolate, promise_ptr.get(), callback, callback_id);
          })
      ->GetHandle();
}

auto Context::GetIdentityHash(BinaryValueHandle* obj_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_.FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_.New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      .RunAndAwait([this, obj_ptr](v8::Isolate* isolate) {
        return object_manipulator_.GetIdentityHash(isolate, obj_ptr.get());
      })
      ->GetHandle();
}

auto Context::GetOwnPropertyNames(BinaryValueHandle* obj_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_.FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_.New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      .RunAndAwait([this, obj_ptr](v8::Isolate* isolate) {
        return object_manipulator_.GetOwnPropertyNames(isolate, obj_ptr.get());
      })
      ->GetHandle();
}

auto Context::GetObjectItem(BinaryValueHandle* obj_handle,
                            BinaryValueHandle* key_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_.FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_.New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  auto key_ptr = bv_factory_.FromHandle(key_handle);
  if (!key_ptr) {
    return bv_factory_.New("Bad handle: key", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      .RunAndAwait([this, obj_ptr, key_ptr](v8::Isolate* isolate) mutable {
        return object_manipulator_.Get(isolate, obj_ptr.get(), key_ptr.get());
      })
      ->GetHandle();
}

auto Context::SetObjectItem(BinaryValueHandle* obj_handle,
                            BinaryValueHandle* key_handle,
                            BinaryValueHandle* val_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_.FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_.New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  auto key_ptr = bv_factory_.FromHandle(key_handle);
  if (!key_ptr) {
    return bv_factory_.New("Bad handle: key", type_value_exception)
        ->GetHandle();
  }

  auto val_ptr = bv_factory_.FromHandle(val_handle);
  if (!val_ptr) {
    return bv_factory_.New("Bad handle: val", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      .RunAndAwait(
          [this, obj_ptr, key_ptr, val_ptr](v8::Isolate* isolate) mutable {
            return object_manipulator_.Set(isolate, obj_ptr.get(),
                                           key_ptr.get(), val_ptr.get());
          })
      ->GetHandle();
}

auto Context::DelObjectItem(BinaryValueHandle* obj_handle,
                            BinaryValueHandle* key_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_.FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_.New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  auto key_ptr = bv_factory_.FromHandle(key_handle);
  if (!key_ptr) {
    return bv_factory_.New("Bad handle: key", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      .RunAndAwait([this, obj_ptr, key_ptr](v8::Isolate* isolate) mutable {
        return object_manipulator_.Del(isolate, obj_ptr.get(), key_ptr.get());
      })
      ->GetHandle();
}

auto Context::SpliceArray(BinaryValueHandle* obj_handle,
                          int32_t start,
                          int32_t delete_count,
                          BinaryValueHandle* new_val_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_.FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_.New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  BinaryValue::Ptr new_val_ptr;
  if (new_val_handle != nullptr) {
    new_val_ptr = bv_factory_.FromHandle(new_val_handle);
    if (!new_val_ptr) {
      return bv_factory_.New("Bad handle: new_val", type_value_exception)
          ->GetHandle();
    }
  }

  return isolate_manager_
      .RunAndAwait([this, obj_ptr, start, delete_count,
                    new_val_ptr](v8::Isolate* isolate) {
        return object_manipulator_.Splice(isolate, obj_ptr.get(), start,
                                          delete_count, new_val_ptr.get());
      })
      ->GetHandle();
}

void Context::FreeBinaryValue(BinaryValueHandle* val) {
  bv_factory_.Free(val);
}

auto Context::CallFunction(BinaryValueHandle* func_handle,
                           BinaryValueHandle* this_handle,
                           BinaryValueHandle* argv_handle,
                           Callback callback,
                           uint64_t callback_id)
    -> std::unique_ptr<CancelableTaskHandle> {
  auto func_ptr = bv_factory_.FromHandle(func_handle);
  if (!func_ptr) {
    auto err = bv_factory_.New("Bad handle: func", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; }, callback,
                   callback_id);
  }

  auto this_ptr = bv_factory_.FromHandle(this_handle);
  if (!this_ptr) {
    auto err = bv_factory_.New("Bad handle: this", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; }, callback,
                   callback_id);
  }

  auto argv_ptr = bv_factory_.FromHandle(argv_handle);
  if (!argv_ptr) {
    auto err = bv_factory_.New("Bad handle: argv", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; }, callback,
                   callback_id);
  }

  return RunTask(
      [this, func_ptr, this_ptr, argv_ptr](v8::Isolate* isolate) {
        return object_manipulator_.Call(isolate, func_ptr.get(), this_ptr.get(),
                                        argv_ptr.get());
      },
      callback, callback_id);
}

auto Context::BinaryValueCount() -> size_t {
  return bv_factory_.Count();
}

}  // end namespace MiniRacer
