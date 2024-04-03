#include "code_evaluator.h"

#include <v8-context.h>
#include <v8-exception.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-message.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-script.h>
#include <v8-value.h>
#include <string>
#include "binary_value.h"
#include "isolate_memory_monitor.h"

namespace MiniRacer {

CodeEvaluator::CodeEvaluator(v8::Persistent<v8::Context>* context,
                             BinaryValueFactory* bv_factory,
                             IsolateMemoryMonitor* memory_monitor)
    : context_(context),
      bv_factory_(bv_factory),
      memory_monitor_(memory_monitor) {}

auto CodeEvaluator::Eval(v8::Isolate* isolate,
                         const std::string& code) -> BinaryValue::Ptr {
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);
  const v8::Local<v8::Context> context = context_->Get(isolate);
  const v8::Context::Scope context_scope(context);

  const v8::TryCatch trycatch(isolate);

  v8::MaybeLocal<v8::String> maybe_string =
      v8::String::NewFromUtf8(isolate, code.data(), v8::NewStringType::kNormal,
                              static_cast<int>(code.size()));

  if (maybe_string.IsEmpty()) {
    // Implies we couldn't convert from utf-8 bytes, which would be odd.
    return bv_factory_->FromString("invalid code string", type_parse_exception);
  }

  // Provide a name just for exception messages:
  v8::ScriptOrigin script_origin(
      v8::String::NewFromUtf8Literal(isolate, "<anonymous>"));

  v8::Local<v8::Script> script;
  if (!v8::Script::Compile(context, maybe_string.ToLocalChecked(),
                           &script_origin)
           .ToLocal(&script) ||
      script.IsEmpty()) {
    return bv_factory_->FromExceptionMessage(context, trycatch.Message(),
                                             trycatch.Exception(),
                                             type_parse_exception);
  }

  v8::MaybeLocal<v8::Value> maybe_value = script->Run(context);
  if (!maybe_value.IsEmpty()) {
    return bv_factory_->FromValue(context, maybe_value.ToLocalChecked());
  }

  // Didn't execute. Find an error:
  if (memory_monitor_->IsHardMemoryLimitReached()) {
    return bv_factory_->FromString("", type_oom_exception);
  }

  BinaryTypes result_type = type_execute_exception;
  if (trycatch.HasTerminated()) {
    result_type = type_terminated_exception;
  }

  return bv_factory_->FromExceptionMessage(context, trycatch.Message(),
                                           trycatch.Exception(), result_type);
}

}  // end namespace MiniRacer
