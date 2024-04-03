#include "promise_attacher.h"
#include <v8-context.h>
#include <v8-external.h>
#include <v8-function-callback.h>
#include <v8-function.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-message.h>
#include <v8-microtask-queue.h>
#include <v8-persistent-handle.h>
#include <v8-promise.h>
#include <memory>
#include "binary_value.h"
#include "callback.h"
#include "gsl_stub.h"
#include "isolate_manager.h"

namespace MiniRacer {

PromiseAttacher::PromiseAttacher(IsolateManager* isolate_manager,
                                 v8::Persistent<v8::Context>* context,
                                 BinaryValueFactory* bv_factory)
    : isolate_manager_(isolate_manager),
      context_(context),
      bv_factory_(bv_factory) {}

void PromiseAttacher::AttachPromiseThen(BinaryValue* bv_ptr,
                                        Callback callback,
                                        void* cb_data) {
  isolate_manager_->RunAndAwait([bv_ptr, callback, cb_data,
                                 this](v8::Isolate* isolate) {
    const v8::Locker lock(isolate);
    const v8::HandleScope scope(isolate);

    const v8::Local<v8::Value> value =
        bv_factory_->GetPersistentHandle(isolate, bv_ptr);
    const v8::Local<v8::Promise> promise = value.As<v8::Promise>();

    // Note that completion_handler will be deleted by whichever callback is
    // called. (If we use auto here we can't mark gsl::owner, so disable this
    // lint check.) NOLINTNEXTLINE(hicpp-use-auto,modernize-use-auto)
    gsl::owner<PromiseCompletionHandler*> completion_handler =
        new PromiseCompletionHandler(bv_factory_, callback, cb_data);
    const v8::Local<v8::External> edata =
        v8::External::New(isolate, completion_handler);

    const v8::Local<v8::Context> context = context_->Get(isolate);
    promise
        ->Then(context,
               v8::Function::New(
                   context, &PromiseCompletionHandler::OnFulfilledStatic, edata)
                   .ToLocalChecked(),
               v8::Function::New(
                   context, &PromiseCompletionHandler::OnRejectedStatic, edata)
                   .ToLocalChecked())
        .ToLocalChecked();
  });
}

PromiseCompletionHandler::PromiseCompletionHandler(
    BinaryValueFactory* bv_factory,
    Callback callback,
    void* cb_data)
    : bv_factory_(bv_factory), callback_(callback), cb_data_(cb_data) {}

void PromiseCompletionHandler::OnFulfilledStatic(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  std::unique_ptr<PromiseCompletionHandler> completion_handler(
      static_cast<PromiseCompletionHandler*>(
          info.Data().As<v8::External>()->Value()));

  completion_handler->OnFulfilled(info.GetIsolate(), info[0]);
}

void PromiseCompletionHandler::OnRejectedStatic(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  std::unique_ptr<PromiseCompletionHandler> completion_handler(
      static_cast<PromiseCompletionHandler*>(
          info.Data().As<v8::External>()->Value()));

  completion_handler->OnRejected(info.GetIsolate(), info[0]);
}

void PromiseCompletionHandler::OnFulfilled(v8::Isolate* isolate,
                                           const v8::Local<v8::Value>& value) {
  const v8::HandleScope scope(isolate);
  const v8::Local<v8::Context> context = isolate->GetCurrentContext();
  const v8::Context::Scope context_scope(context);

  BinaryValue::Ptr val = bv_factory_->FromValue(context, value);

  callback_(cb_data_, val.release());
}

void PromiseCompletionHandler::OnRejected(v8::Isolate* isolate,
                                          const v8::Local<v8::Value>& exc) {
  const v8::HandleScope scope(isolate);
  const v8::Local<v8::Context> context = isolate->GetCurrentContext();
  const v8::Context::Scope context_scope(context);

  BinaryValue::Ptr val = bv_factory_->FromExceptionMessage(
      context, v8::Local<v8::Message>(), exc, type_execute_exception);

  callback_(cb_data_, val.release());
}

}  // end namespace MiniRacer
