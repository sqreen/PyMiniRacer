#ifndef INCLUDE_MINI_RACER_PROMISE_ATTACHER_H
#define INCLUDE_MINI_RACER_PROMISE_ATTACHER_H

#include <v8-context.h>
#include <v8-function-callback.h>
#include <v8-isolate.h>
#include <v8-persistent-handle.h>
#include <cstdint>
#include <memory>
#include "binary_value.h"
#include "callback.h"
#include "context_holder.h"

namespace MiniRacer {

class PromiseAttacher {
 public:
  PromiseAttacher(std::shared_ptr<ContextHolder> context,
                  std::shared_ptr<BinaryValueFactory> bv_factory);

  auto AttachPromiseThen(v8::Isolate* isolate,
                         BinaryValue* promise_ptr,
                         Callback callback,
                         uint64_t callback_id) -> BinaryValue::Ptr;

 private:
  std::shared_ptr<ContextHolder> context_;
  std::shared_ptr<BinaryValueFactory> bv_factory_;
};

class PromiseCompletionHandler {
 public:
  PromiseCompletionHandler(std::shared_ptr<BinaryValueFactory> bv_factory,
                           Callback callback_,
                           uint64_t callback_id);

  static void OnFulfilledStatic(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  static void OnRejectedStatic(const v8::FunctionCallbackInfo<v8::Value>& info);

 private:
  void OnFulfilled(v8::Isolate* isolate, const v8::Local<v8::Value>& value);
  void OnRejected(v8::Isolate* isolate, const v8::Local<v8::Value>& exc);

  std::shared_ptr<BinaryValueFactory> bv_factory_;
  Callback callback_;
  uint64_t callback_id_;
};

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_PROMISE_ATTACHER_H
