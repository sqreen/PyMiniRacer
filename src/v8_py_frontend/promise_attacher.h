#ifndef INCLUDE_MINI_RACER_PROMISE_ATTACHER_H
#define INCLUDE_MINI_RACER_PROMISE_ATTACHER_H

#include <v8-context.h>
#include <v8-function-callback.h>
#include <v8-isolate.h>
#include <v8-persistent-handle.h>
#include "binary_value.h"
#include "callback.h"
#include "isolate_manager.h"

namespace MiniRacer {

class PromiseAttacher {
 public:
  PromiseAttacher(IsolateManager* isolate_manager,
                  v8::Persistent<v8::Context>* context,
                  BinaryValueFactory* bv_factory);

  void AttachPromiseThen(BinaryValue* bv_ptr, Callback callback, void* cb_data);

 private:
  IsolateManager* isolate_manager_;
  v8::Persistent<v8::Context>* context_;
  BinaryValueFactory* bv_factory_;
};

class PromiseCompletionHandler {
 public:
  PromiseCompletionHandler(BinaryValueFactory* bv_factory,
                           Callback callback_,
                           void* cb_data);

  static void OnFulfilledStatic(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  static void OnRejectedStatic(const v8::FunctionCallbackInfo<v8::Value>& info);

 private:
  void OnFulfilled(v8::Isolate* isolate, const v8::Local<v8::Value>& value);
  void OnRejected(v8::Isolate* isolate, const v8::Local<v8::Value>& exc);

  BinaryValueFactory* bv_factory_;
  Callback callback_;
  void* cb_data_;
};

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_PROMISE_ATTACHER_H
