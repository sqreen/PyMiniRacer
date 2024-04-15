#ifndef INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H
#define INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H

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

/** Wraps a JS callback wrapped around the given C callback function pointer. */
class JSCallbackMaker {
 public:
  JSCallbackMaker(std::shared_ptr<ContextHolder> context,
                  std::shared_ptr<BinaryValueFactory> bv_factory,
                  Callback callback);

  auto MakeJSCallback(v8::Isolate* isolate,
                      uint64_t callback_id) -> BinaryValue::Ptr;

 private:
  std::shared_ptr<ContextHolder> context_;
  std::shared_ptr<BinaryValueFactory> bv_factory_;
  Callback callback_;
};

class CallbackHandler {
 public:
  CallbackHandler(std::shared_ptr<BinaryValueFactory> bv_factory,
                  Callback callback_,
                  uint64_t callback_id);

  static void OnCalledStatic(const v8::FunctionCallbackInfo<v8::Value>& info);

 private:
  void OnCalled(v8::Isolate* isolate,
                const v8::FunctionCallbackInfo<v8::Value>& info);

  std::shared_ptr<BinaryValueFactory> bv_factory_;
  Callback callback_;
  uint64_t callback_id_;
};

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H
