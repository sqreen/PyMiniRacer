#include "context.h"
#include <v8-initialization.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-persistent-handle.h>
#include <v8-platform.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "cancelable_task_runner.h"
#include "code_evaluator.h"
#include "context_holder.h"
#include "heap_reporter.h"
#include "isolate_manager.h"
#include "isolate_memory_monitor.h"
#include "js_callback_maker.h"
#include "object_manipulator.h"

namespace MiniRacer {

Context::Context(v8::Platform* platform, Callback callback)
    : callback_(callback),
      isolate_manager_(std::make_shared<IsolateManager>(platform)),
      isolate_memory_monitor_(
          std::make_shared<IsolateMemoryMonitor>(isolate_manager_)),
      bv_factory_(std::make_shared<BinaryValueFactory>(isolate_manager_)),
      context_holder_(std::make_shared<ContextHolder>(isolate_manager_)),
      js_callback_maker_(std::make_shared<JSCallbackMaker>(context_holder_,
                                                           bv_factory_,
                                                           callback_)),
      code_evaluator_(std::make_shared<CodeEvaluator>(context_holder_,
                                                      bv_factory_,
                                                      isolate_memory_monitor_)),
      heap_reporter_(std::make_shared<HeapReporter>(bv_factory_)),
      object_manipulator_(
          std::make_shared<ObjectManipulator>(context_holder_, bv_factory_)),
      cancelable_task_runner_(
          std::make_shared<CancelableTaskRunner>(isolate_manager_)) {}

auto Context::MakeJSCallback(uint64_t callback_id) -> BinaryValueHandle* {
  return isolate_manager_
      ->RunAndAwait([js_callback_maker = js_callback_maker_,
                     callback_id](v8::Isolate* isolate) {
        return js_callback_maker->MakeJSCallback(isolate, callback_id);
      })
      ->GetHandle();
}

template <typename Runnable>
auto Context::RunTask(Runnable runnable, uint64_t callback_id) -> uint64_t {
  // Start an async task!

  return cancelable_task_runner_->Schedule(
      /*runnable=*/
      std::move(runnable),
      /*on_completed=*/
      [callback = callback_, callback_id](const BinaryValue::Ptr& val) {
        callback(callback_id, val->GetHandle());
      },
      /*on_canceled=*/
      [callback = callback_, callback_id,
       bv_factory = bv_factory_](const BinaryValue::Ptr& val) {
        if (val) {
          // Ignore the produced value, if any:
          bv_factory->Free(val->GetHandle());
        }

        auto err =
            bv_factory->New("execution terminated", type_terminated_exception);
        callback(callback_id, err->GetHandle());
      });
}

auto Context::Eval(BinaryValueHandle* code_handle,
                   uint64_t callback_id) -> uint64_t {
  auto code_ptr = bv_factory_->FromHandle(code_handle);
  if (!code_ptr) {
    auto err = bv_factory_->New("Bad handle: code", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; },
                   callback_id);
  }

  return RunTask(
      [code_ptr, code_evaluator = code_evaluator_](v8::Isolate* isolate) {
        return code_evaluator->Eval(isolate, code_ptr.get());
      },
      callback_id);
}

void Context::CancelTask(uint64_t task_id) {
  cancelable_task_runner_->Cancel(task_id);
}

auto Context::HeapSnapshot(uint64_t callback_id) -> uint64_t {
  return RunTask(
      [heap_reporter = heap_reporter_](v8::Isolate* isolate) {
        return heap_reporter->HeapSnapshot(isolate);
      },
      callback_id);
}

auto Context::HeapStats(uint64_t callback_id) -> uint64_t {
  return RunTask(
      [heap_reporter = heap_reporter_](v8::Isolate* isolate) {
        return heap_reporter->HeapStats(isolate);
      },
      callback_id);
}

auto Context::GetIdentityHash(BinaryValueHandle* obj_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_->FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_->New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      ->RunAndAwait([object_manipulator = object_manipulator_,
                     obj_ptr](v8::Isolate* isolate) {
        return object_manipulator->GetIdentityHash(isolate, obj_ptr.get());
      })
      ->GetHandle();
}

auto Context::GetOwnPropertyNames(BinaryValueHandle* obj_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_->FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_->New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      ->RunAndAwait([object_manipulator = object_manipulator_,
                     obj_ptr](v8::Isolate* isolate) {
        return object_manipulator->GetOwnPropertyNames(isolate, obj_ptr.get());
      })
      ->GetHandle();
}

auto Context::GetObjectItem(BinaryValueHandle* obj_handle,
                            BinaryValueHandle* key_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_->FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_->New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  auto key_ptr = bv_factory_->FromHandle(key_handle);
  if (!key_ptr) {
    return bv_factory_->New("Bad handle: key", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      ->RunAndAwait([object_manipulator = object_manipulator_, obj_ptr,
                     key_ptr](v8::Isolate* isolate) mutable {
        return object_manipulator->Get(isolate, obj_ptr.get(), key_ptr.get());
      })
      ->GetHandle();
}

auto Context::SetObjectItem(BinaryValueHandle* obj_handle,
                            BinaryValueHandle* key_handle,
                            BinaryValueHandle* val_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_->FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_->New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  auto key_ptr = bv_factory_->FromHandle(key_handle);
  if (!key_ptr) {
    return bv_factory_->New("Bad handle: key", type_value_exception)
        ->GetHandle();
  }

  auto val_ptr = bv_factory_->FromHandle(val_handle);
  if (!val_ptr) {
    return bv_factory_->New("Bad handle: val", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      ->RunAndAwait([object_manipulator = object_manipulator_, obj_ptr, key_ptr,
                     val_ptr](v8::Isolate* isolate) mutable {
        return object_manipulator->Set(isolate, obj_ptr.get(), key_ptr.get(),
                                       val_ptr.get());
      })
      ->GetHandle();
}

auto Context::DelObjectItem(BinaryValueHandle* obj_handle,
                            BinaryValueHandle* key_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_->FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_->New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  auto key_ptr = bv_factory_->FromHandle(key_handle);
  if (!key_ptr) {
    return bv_factory_->New("Bad handle: key", type_value_exception)
        ->GetHandle();
  }

  return isolate_manager_
      ->RunAndAwait([object_manipulator = object_manipulator_, obj_ptr,
                     key_ptr](v8::Isolate* isolate) mutable {
        return object_manipulator->Del(isolate, obj_ptr.get(), key_ptr.get());
      })
      ->GetHandle();
}

auto Context::SpliceArray(BinaryValueHandle* obj_handle,
                          int32_t start,
                          int32_t delete_count,
                          BinaryValueHandle* new_val_handle)
    -> BinaryValueHandle* {
  auto obj_ptr = bv_factory_->FromHandle(obj_handle);
  if (!obj_ptr) {
    return bv_factory_->New("Bad handle: obj", type_value_exception)
        ->GetHandle();
  }

  BinaryValue::Ptr new_val_ptr;
  if (new_val_handle != nullptr) {
    new_val_ptr = bv_factory_->FromHandle(new_val_handle);
    if (!new_val_ptr) {
      return bv_factory_->New("Bad handle: new_val", type_value_exception)
          ->GetHandle();
    }
  }

  return isolate_manager_
      ->RunAndAwait([object_manipulator = object_manipulator_, obj_ptr, start,
                     delete_count, new_val_ptr](v8::Isolate* isolate) {
        return object_manipulator->Splice(isolate, obj_ptr.get(), start,
                                          delete_count, new_val_ptr.get());
      })
      ->GetHandle();
}

void Context::FreeBinaryValue(BinaryValueHandle* val) {
  bv_factory_->Free(val);
}

auto Context::CallFunction(BinaryValueHandle* func_handle,
                           BinaryValueHandle* this_handle,
                           BinaryValueHandle* argv_handle,
                           uint64_t callback_id) -> uint64_t {
  auto func_ptr = bv_factory_->FromHandle(func_handle);
  if (!func_ptr) {
    auto err = bv_factory_->New("Bad handle: func", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; },
                   callback_id);
  }

  auto this_ptr = bv_factory_->FromHandle(this_handle);
  if (!this_ptr) {
    auto err = bv_factory_->New("Bad handle: this", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; },
                   callback_id);
  }

  auto argv_ptr = bv_factory_->FromHandle(argv_handle);
  if (!argv_ptr) {
    auto err = bv_factory_->New("Bad handle: argv", type_value_exception);
    return RunTask([err](v8::Isolate* /*isolate*/) { return err; },
                   callback_id);
  }

  return RunTask(
      [object_manipulator = object_manipulator_, func_ptr, this_ptr,
       argv_ptr](v8::Isolate* isolate) {
        return object_manipulator->Call(isolate, func_ptr.get(), this_ptr.get(),
                                        argv_ptr.get());
      },
      callback_id);
}

auto Context::BinaryValueCount() -> size_t {
  return bv_factory_->Count();
}

}  // end namespace MiniRacer
