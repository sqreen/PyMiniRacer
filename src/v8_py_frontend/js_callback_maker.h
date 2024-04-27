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
#include <unordered_map>
#include "binary_value.h"
#include "callback.h"
#include "context_holder.h"

namespace MiniRacer {

/** A callback caller contains the a bundle of items needed to successfully
 * handle a callback from JS. This is affine to a single MiniRacer::Context (and
 * so multiple callbacks can share a context).
 */
class JSCallbackCaller {
 public:
  JSCallbackCaller(std::shared_ptr<BinaryValueFactory> bv_factory,
                   Callback callback);

  void DoCallback(v8::Local<v8::Context> context,
                  uint64_t callback_id,
                  v8::Local<v8::Array> args);

 private:
  std::shared_ptr<BinaryValueFactory> bv_factory_;
  Callback callback_;
};

class JSCallbackCallerRegistry {
 public:
  static auto Get() -> JSCallbackCallerRegistry*;

  auto Register(std::shared_ptr<BinaryValueFactory> bv_factory,
                Callback callback) -> uint64_t;
  void Unregister(uint64_t callback_caller_id);

  auto Get(uint64_t callback_caller_id) -> std::shared_ptr<JSCallbackCaller>;

 private:
  static JSCallbackCallerRegistry singleton_;
  std::mutex mutex_;
  uint64_t next_id_{0};
  std::unordered_map<uint64_t, std::shared_ptr<JSCallbackCaller>>
      callback_callers_;
};

class JSCallbackCallerHolder {
 public:
  JSCallbackCallerHolder(std::shared_ptr<BinaryValueFactory> bv_factory,
                         Callback callback);

  ~JSCallbackCallerHolder();

  JSCallbackCallerHolder(const JSCallbackCallerHolder&) = delete;
  auto operator=(const JSCallbackCallerHolder&) -> JSCallbackCallerHolder& =
                                                       delete;
  JSCallbackCallerHolder(JSCallbackCallerHolder&&) = delete;
  auto operator=(JSCallbackCallerHolder&& other) -> JSCallbackCallerHolder& =
                                                        delete;

  [[nodiscard]] auto Get() const -> uint64_t;

 private:
  uint64_t callback_caller_id_;
};

/** Wraps a JS callback wrapped around the given C callback function pointer. */
class JSCallbackMaker {
 public:
  JSCallbackMaker(std::shared_ptr<ContextHolder> context_holder,
                  const std::shared_ptr<BinaryValueFactory>& bv_factory,
                  Callback callback);

  auto MakeJSCallback(v8::Isolate* isolate,
                      uint64_t callback_id) -> BinaryValue::Ptr;

 private:
  static void OnCalledStatic(const v8::FunctionCallbackInfo<v8::Value>& info);

  std::shared_ptr<ContextHolder> context_holder_;
  std::shared_ptr<BinaryValueFactory> bv_factory_;
  JSCallbackCallerHolder callback_caller_holder_;
};

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_JS_CALLBACK_MAKER_H
