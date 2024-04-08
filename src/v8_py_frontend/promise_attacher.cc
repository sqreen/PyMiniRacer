#include "promise_attacher.h"
#include <v8-context.h>
#include <v8-external.h>
#include <v8-function-callback.h>
#include <v8-function.h>
#include <v8-local-handle.h>
#include <v8-message.h>
#include <v8-microtask-queue.h>
#include <v8-persistent-handle.h>
#include <v8-promise.h>
#include <cstdint>
#include <memory>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "context_holder.h"

namespace MiniRacer {

PromiseAttacher::PromiseAttacher(std::shared_ptr<ContextHolder> context,
                                 std::shared_ptr<BinaryValueFactory> bv_factory)
    : context_(std::move(context)), bv_factory_(std::move(bv_factory)) {}

auto PromiseAttacher::AttachPromiseThen(v8::Isolate* isolate,
                                        BinaryValue* promise_ptr,
                                        Callback callback,
                                        uint64_t callback_id)
    -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  const v8::Local<v8::Value> local_promise_val =
      promise_ptr->ToValue(local_context);
  const v8::Local<v8::Promise> local_promise =
      local_promise_val.As<v8::Promise>();

  // Note that completion_handler will be deleted by whichever callback is
  // called. (If we use auto here we can't mark gsl::owner, so disable this
  // lint check:)

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto* completion_handler =
      new PromiseCompletionHandler(bv_factory_, callback, callback_id);
  const v8::Local<v8::External> edata =
      v8::External::New(isolate, completion_handler);

  local_promise
      ->Then(
          local_context,
          v8::Function::New(local_context,
                            &PromiseCompletionHandler::OnFulfilledStatic, edata)
              .ToLocalChecked(),
          v8::Function::New(local_context,
                            &PromiseCompletionHandler::OnRejectedStatic, edata)
              .ToLocalChecked())
      .ToLocalChecked();

  return bv_factory_->New(true);
}

PromiseCompletionHandler::PromiseCompletionHandler(
    std::shared_ptr<BinaryValueFactory> bv_factory,
    Callback callback,
    uint64_t callback_id)
    : bv_factory_(std::move(bv_factory)),
      callback_(callback),
      callback_id_(callback_id) {}

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

  const BinaryValue::Ptr val = bv_factory_->New(context, value);

  callback_(callback_id_, val->GetHandle());
}

void PromiseCompletionHandler::OnRejected(v8::Isolate* isolate,
                                          const v8::Local<v8::Value>& exc) {
  const v8::HandleScope scope(isolate);
  const v8::Local<v8::Context> context = isolate->GetCurrentContext();
  const v8::Context::Scope context_scope(context);

  const BinaryValue::Ptr val = bv_factory_->New(
      context, v8::Local<v8::Message>(), exc, type_execute_exception);

  callback_(callback_id_, val->GetHandle());
}

}  // end namespace MiniRacer
