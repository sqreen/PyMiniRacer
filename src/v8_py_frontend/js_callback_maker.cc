#include "js_callback_maker.h"
#include <v8-container.h>
#include <v8-context.h>
#include <v8-external.h>
#include <v8-function-callback.h>
#include <v8-function.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
#include <cstdint>
#include <memory>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "context_holder.h"

namespace MiniRacer {

JSCallbackMaker::JSCallbackMaker(std::shared_ptr<ContextHolder> context,
                                 std::shared_ptr<BinaryValueFactory> bv_factory,
                                 Callback callback)
    : context_(std::move(context)),
      bv_factory_(std::move(bv_factory)),
      callback_(callback) {}

auto JSCallbackMaker::MakeJSCallback(v8::Isolate* isolate,
                                     uint64_t callback_id) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> local_context = context_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(local_context);

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto* callback_handler =
      new CallbackHandler(bv_factory_, callback_, callback_id);
  const v8::Local<v8::External> edata =
      v8::External::New(isolate, callback_handler);

  const v8::Local<v8::Function> func =
      v8::Function::New(local_context, &CallbackHandler::OnCalledStatic, edata)
          .ToLocalChecked();

  return bv_factory_->New(local_context, func);
}

CallbackHandler::CallbackHandler(std::shared_ptr<BinaryValueFactory> bv_factory,
                                 Callback callback,
                                 uint64_t callback_id)
    : bv_factory_(std::move(bv_factory)),
      callback_(callback),
      callback_id_(callback_id) {}

void CallbackHandler::OnCalledStatic(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  std::unique_ptr<CallbackHandler> callback_handler(
      static_cast<CallbackHandler*>(info.Data().As<v8::External>()->Value()));

  callback_handler->OnCalled(info.GetIsolate(), info);
}

void CallbackHandler::OnCalled(
    v8::Isolate* isolate,
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  const v8::HandleScope scope(isolate);
  const v8::Local<v8::Context> context = isolate->GetCurrentContext();
  const v8::Context::Scope context_scope(context);

  int idx = 0;
  const v8::Local<v8::Array> args =
      v8::Array::New(context, info.Length(), [&idx, &info] {
        return info[idx++];
      }).ToLocalChecked();

  const BinaryValue::Ptr args_ptr = bv_factory_->New(context, args);

  callback_(callback_id_, args_ptr->GetHandle());
}

}  // end namespace MiniRacer
