#include "code_evaluator.h"

#include <v8-context.h>
#include <v8-exception.h>
#include <v8-function.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-message.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-script.h>
#include <v8-value.h>
#include <string>
#include "binary_value.h"
#include "isolate_memory_monitor.h"

namespace MiniRacer {

CodeEvaluator::CodeEvaluator(v8::Isolate* isolate,
                             v8::Persistent<v8::Context>* context,
                             BinaryValueFactory* bv_factory,
                             IsolateMemoryMonitor* memory_monitor)
    : isolate_(isolate),
      context_(context),
      bv_factory_(bv_factory),
      memory_monitor_(memory_monitor) {}

auto CodeEvaluator::SummarizeTryCatch(v8::Local<v8::Context>& context,
                                      const v8::TryCatch& trycatch)
    -> BinaryValue::Ptr {
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

auto CodeEvaluator::GetFunction(const std::string& code,
                                v8::Local<v8::Context>& context,
                                v8::Local<v8::Function>* func) -> bool {
  // Is it a single function call?
  // Does the code string end with '()'?
  if (code.size() < 3 || code[code.size() - 2] != '(' ||
      code[code.size() - 1] != ')') {
    return false;
  }

  // Check if the value before the () is a callable identifier:
  const v8::MaybeLocal<v8::String> maybe_identifier =
      v8::String::NewFromUtf8(isolate_, code.data(), v8::NewStringType::kNormal,
                              static_cast<int>(code.size() - 2));
  v8::Local<v8::String> identifier;
  if (!maybe_identifier.ToLocal(&identifier)) {
    return false;
  }

  v8::Local<v8::Value> func_val;
  if (!context->Global()->Get(context, identifier).ToLocal(&func_val)) {
    return false;
  }

  if (!func_val->IsFunction()) {
    return false;
  }

  *func = func_val.As<v8::Function>();
  return true;
}

auto CodeEvaluator::EvalFunction(const v8::Local<v8::Function>& func,
                                 v8::Local<v8::Context>& context)
    -> BinaryValue::Ptr {
  const v8::TryCatch trycatch(isolate_);

  v8::MaybeLocal<v8::Value> maybe_value =
      func->Call(context, v8::Undefined(isolate_), 0, {});
  if (!maybe_value.IsEmpty()) {
    return bv_factory_->FromValue(context, maybe_value.ToLocalChecked());
  }

  return SummarizeTryCatch(context, trycatch);
}

auto CodeEvaluator::EvalAsScript(const std::string& code,
                                 v8::Local<v8::Context>& context)
    -> BinaryValue::Ptr {
  const v8::TryCatch trycatch(isolate_);

  v8::MaybeLocal<v8::String> maybe_string =
      v8::String::NewFromUtf8(isolate_, code.data(), v8::NewStringType::kNormal,
                              static_cast<int>(code.size()));

  if (maybe_string.IsEmpty()) {
    // Implies we couldn't convert from utf-8 bytes, which would be odd.
    return bv_factory_->FromString("invalid code string", type_parse_exception);
  }

  // Provide a name just for exception messages:
  v8::ScriptOrigin script_origin(
      v8::String::NewFromUtf8Literal(isolate_, "<anonymous>"));

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
  return SummarizeTryCatch(context, trycatch);
}

auto CodeEvaluator::Eval(const std::string& code) -> BinaryValue::Ptr {
  const v8::Locker lock(isolate_);
  const v8::Isolate::Scope isolate_scope(isolate_);
  const v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Context> context = context_->Get(isolate_);
  const v8::Context::Scope context_scope(context);

  // Try and evaluate as a simple function call.
  // This gets us a speedup of about 1.17 (i.e., 17% faster) on a baseline of
  // no-op function calls. It's not much, but it provides an easy way for users
  // to eek out performance without exposing a pre-compiled scripts API: users
  // can simply globally *define* all their functions in one (or more) eval
  // call(s), and then *call* them in another.
  v8::Local<v8::Function> func;
  if (GetFunction(code, context, &func)) {
    function_eval_call_count_++;
    return EvalFunction(func, context);
  }

  // Fall back on a slower full eval:
  full_eval_call_count_++;
  return EvalAsScript(code, context);
}

}  // end namespace MiniRacer
