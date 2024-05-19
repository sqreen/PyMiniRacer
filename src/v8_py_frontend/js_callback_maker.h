#ifndef INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H
#define INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H

#include <v8-container.h>
#include <v8-context.h>
#include <v8-function-callback.h>
#include <v8-isolate.h>
#include <v8-persistent-handle.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include "binary_value.h"
#include "callback.h"
#include "context_holder.h"
#include "id_maker.h"

namespace MiniRacer {

/** A callback caller contains the bundle of items needed to successfully
 * handle a callback from JS by calling through to the MiniRacer user (i.e.,
 * Python). A JSCallbackCaller is affine to a single MiniRacer::Context (and so
 * multiple callbacks can share a single JSCallbackCaller).
 */
class JSCallbackCaller {
 public:
  JSCallbackCaller(BinaryValueFactory* bv_factory,
                   RememberValueAndCallback callback);

  void DoCallback(v8::Local<v8::Context> context,
                  uint64_t callback_id,
                  v8::Local<v8::Array> args);

 private:
  BinaryValueFactory* bv_factory_;
  RememberValueAndCallback callback_;
};

/** Creates a JS callback wrapped around the given C callback function pointer.
 */
class JSCallbackMaker {
 public:
  JSCallbackMaker(ContextHolder* context_holder,
                  BinaryValueFactory* bv_factory,
                  RememberValueAndCallback callback);

  auto MakeJSCallback(v8::Isolate* isolate,
                      uint64_t callback_id) -> BinaryValue::Ptr;

 private:
  static void OnCalledStatic(const v8::FunctionCallbackInfo<v8::Value>& info);
  static auto GetCallbackCallers()
      -> std::shared_ptr<IdMaker<JSCallbackCaller>>;

  static std::shared_ptr<IdMaker<JSCallbackCaller>> callback_callers_;
  static std::once_flag callback_callers_init_flag_;

  ContextHolder* context_holder_;
  BinaryValueFactory* bv_factory_;
  IdHolder<JSCallbackCaller> callback_caller_holder_;
};

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H
