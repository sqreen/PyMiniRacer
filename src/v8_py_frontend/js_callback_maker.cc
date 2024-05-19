#include "js_callback_maker.h"
#include <v8-container.h>
#include <v8-context.h>
#include <v8-function-callback.h>
#include <v8-function.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include "binary_value.h"
#include "callback.h"
#include "context_holder.h"
#include "id_maker.h"

namespace MiniRacer {

JSCallbackCaller::JSCallbackCaller(BinaryValueFactory* bv_factory,
                                   RememberValueAndCallback callback)
    : bv_factory_(bv_factory), callback_(std::move(callback)) {}

void JSCallbackCaller::DoCallback(v8::Local<v8::Context> context,
                                  uint64_t callback_id,
                                  v8::Local<v8::Array> args) {
  const BinaryValue::Ptr args_ptr = bv_factory_->New(context, args);
  callback_(callback_id, args_ptr);
}

std::shared_ptr<IdMaker<JSCallbackCaller>> JSCallbackMaker::callback_callers_;
std::once_flag JSCallbackMaker::callback_callers_init_flag_;

auto JSCallbackMaker::GetCallbackCallers()
    -> std::shared_ptr<IdMaker<JSCallbackCaller>> {
  std::call_once(callback_callers_init_flag_, []() {
    callback_callers_ = std::make_shared<IdMaker<JSCallbackCaller>>();
  });
  return callback_callers_;
}

JSCallbackMaker::JSCallbackMaker(ContextHolder* context_holder,
                                 BinaryValueFactory* bv_factory,
                                 RememberValueAndCallback callback)
    : context_holder_(context_holder),
      bv_factory_(bv_factory),
      callback_caller_holder_(
          std::make_shared<JSCallbackCaller>(bv_factory, std::move(callback)),
          GetCallbackCallers()) {}

auto JSCallbackMaker::MakeJSCallback(v8::Isolate* isolate,
                                     uint64_t callback_id) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> context = context_holder_->Get()->Get(isolate);
  const v8::Context::Scope context_scope(context);

  // We create a JS Array containing:
  // {a BigInt indicating the callback caller ID, a BigInt indicating the
  // callback ID}
  // ... And stuff it into the callback, so we can understand the context when
  // we're called back.
  // We do this instead of embedding pointers to C++ objects in the objects
  // (using v8::External) so that we can control object teardown. In this model,
  // we tear down the C++ JSCallbackMaker and its dependencies when the
  // MiniRacer::Context is torn down, and if a callback executes after the
  // underlying callback caller is torn down, that callback is safely ignored.
  const v8::Local<v8::BigInt> callback_caller_id_bigint =
      v8::BigInt::NewFromUnsigned(isolate, callback_caller_holder_.GetId());
  const v8::Local<v8::BigInt> callback_id_bigint =
      v8::BigInt::NewFromUnsigned(isolate, callback_id);
  std::array<v8::Local<v8::Value>, 2> data_elements = {
      callback_caller_id_bigint, callback_id_bigint};

  const v8::Local<v8::Array> data =
      v8::Array::New(isolate, data_elements.data(), data_elements.size());

  const v8::Local<v8::Function> func =
      v8::Function::New(context, &JSCallbackMaker::OnCalledStatic, data)
          .ToLocalChecked();

  return bv_factory_->New(context, func);
}

void JSCallbackMaker::OnCalledStatic(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  const v8::HandleScope scope(isolate);
  const v8::Local<v8::Context> context = isolate->GetCurrentContext();
  const v8::Context::Scope context_scope(context);

  const v8::Local<v8::Value> data_value = info.Data();

  if (!data_value->IsArray()) {
    return;
  }
  const v8::Local<v8::Array> data_array = data_value.As<v8::Array>();

  if (data_array->Length() != 2) {
    return;
  }
  const v8::MaybeLocal<v8::Value> callback_caller_id_value_maybe =
      data_array->Get(context, 0);
  const v8::MaybeLocal<v8::Value> callback_id_value_maybe =
      data_array->Get(context, 1);

  v8::Local<v8::Value> callback_caller_id_value;
  if (!callback_caller_id_value_maybe.ToLocal(&callback_caller_id_value)) {
    return;
  }

  if (!callback_caller_id_value->IsBigInt()) {
    return;
  }
  const v8::Local<v8::BigInt> callback_caller_id_bigint =
      callback_caller_id_value.As<v8::BigInt>();

  bool lossless = false;
  const uint64_t callback_caller_id =
      callback_caller_id_bigint->Uint64Value(&lossless);
  if (!lossless) {
    return;
  }

  v8::Local<v8::Value> callback_id_value;
  if (!callback_id_value_maybe.ToLocal(&callback_id_value)) {
    return;
  }

  if (!callback_id_value->IsBigInt()) {
    return;
  }
  const v8::Local<v8::BigInt> callback_id_bigint =
      callback_id_value.As<v8::BigInt>();

  const uint64_t callback_id = callback_id_bigint->Uint64Value(&lossless);
  if (!lossless) {
    return;
  }

  int idx = 0;
  const v8::Local<v8::Array> args =
      v8::Array::New(context, info.Length(), [&idx, &info] {
        return info[idx++];
      }).ToLocalChecked();

  const std::shared_ptr<JSCallbackCaller> callback_caller =
      callback_callers_->GetObject(callback_caller_id);
  if (!callback_caller) {
    return;
  }

  callback_caller->DoCallback(context, callback_id, args);
}

}  // end namespace MiniRacer
