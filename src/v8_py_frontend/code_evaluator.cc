#include "code_evaluator.h"

#include <v8-context.h>
#include <v8-exception.h>
#include <v8-function.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-script.h>
#include <v8-value.h>
#include <cstdint>
#include <optional>
#include <string>
#include "binary_value.h"
#include "breaker_thread.h"
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
                                      const v8::TryCatch& trycatch,
                                      BinaryTypes resultType)
    -> BinaryValue::Ptr {
  if (!trycatch.StackTrace(context).IsEmpty()) {
    v8::Local<v8::Value> stacktrace;

    if (trycatch.StackTrace(context).ToLocal(&stacktrace)) {
      std::optional<std::string> backtrace = ValueToUtf8String(stacktrace);
      if (backtrace.has_value()) {
        // Generally the backtrace from v8 starts with the exception message, so
        // we can skip the exception message (below) when we have the backtrace.
        return bv_factory_->New(backtrace.value(), resultType);
      }
    }
  }

  // Fall back to the backtrace-less exception message:
  if (!trycatch.Exception()->IsNull()) {
    std::optional<std::string> message =
        ValueToUtf8String(trycatch.Exception());
    if (message.has_value()) {
      return bv_factory_->New(message.value(), resultType);
    }
  }

  // Send no message at all; the recipient can fill in generic messages based on
  // the type code.
  return bv_factory_->New("", resultType);
}

auto CodeEvaluator::SummarizeTryCatchAfterExecution(
    v8::Local<v8::Context>& context,
    const v8::TryCatch& trycatch,
    const BreakerThread& breaker_thread) -> BinaryValue::Ptr {
  BinaryTypes resultType = type_execute_exception;

  if (memory_monitor_->IsHardMemoryLimitReached()) {
    resultType = type_oom_exception;
  } else if (breaker_thread.timed_out()) {
    resultType = type_timeout_exception;
  } else if (trycatch.HasTerminated()) {
    resultType = type_terminated_exception;
  }

  return SummarizeTryCatch(context, trycatch, resultType);
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
                                 v8::Local<v8::Context>& context,
                                 const BreakerThread& breaker_thread)
    -> BinaryValue::Ptr {
  const v8::TryCatch trycatch(isolate_);

  v8::MaybeLocal<v8::Value> maybe_value =
      func->Call(context, v8::Undefined(isolate_), 0, {});
  if (!maybe_value.IsEmpty()) {
    return bv_factory_->ConvertFromV8(context, maybe_value.ToLocalChecked());
  }

  return SummarizeTryCatchAfterExecution(context, trycatch, breaker_thread);
}

auto CodeEvaluator::EvalAsScript(const std::string& code,
                                 v8::Local<v8::Context>& context,
                                 const BreakerThread& breaker_thread)
    -> BinaryValue::Ptr {
  const v8::TryCatch trycatch(isolate_);

  v8::MaybeLocal<v8::String> maybe_string =
      v8::String::NewFromUtf8(isolate_, code.data(), v8::NewStringType::kNormal,
                              static_cast<int>(code.size()));

  if (maybe_string.IsEmpty()) {
    // Implies we couldn't convert from utf-8 bytes, which would be odd.
    return bv_factory_->New("invalid code string", type_parse_exception);
  }

  v8::Local<v8::Script> script;
  if (!v8::Script::Compile(context, maybe_string.ToLocalChecked())
           .ToLocal(&script) ||
      script.IsEmpty()) {
    return SummarizeTryCatch(context, trycatch, type_parse_exception);
  }

  v8::MaybeLocal<v8::Value> maybe_value = script->Run(context);
  if (!maybe_value.IsEmpty()) {
    return bv_factory_->ConvertFromV8(context, maybe_value.ToLocalChecked());
  }

  // Didn't execute. Find an error:
  return SummarizeTryCatchAfterExecution(context, trycatch, breaker_thread);
}

auto CodeEvaluator::Eval(const std::string& code,
                         uint64_t timeout) -> BinaryValue::Ptr {
  const v8::Locker lock(isolate_);
  const v8::Isolate::Scope isolate_scope(isolate_);
  const v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Context> context = context_->Get(isolate_);
  const v8::Context::Scope context_scope(context);

  // Spawn a thread to enforce the timeout limit:
  const BreakerThread breaker_thread(isolate_, timeout);

  // Try and evaluate as a simple function call.
  // This gets us about a speedup of about 1.17 (i.e., 17% faster) on a baseline
  // of no-op function calls.
  v8::Local<v8::Function> func;
  if (GetFunction(code, context, &func)) {
    function_eval_call_count_++;
    return EvalFunction(func, context, breaker_thread);
  }

  // Fall back on a slower full eval:
  full_eval_call_count_++;
  return EvalAsScript(code, context, breaker_thread);
}

auto CodeEvaluator::ValueToUtf8String(v8::Local<v8::Value> value)
    -> std::optional<std::string> {
  v8::String::Utf8Value utf8(isolate_, value);

  if (utf8.length() != 0) {
    return std::make_optional(std::string(*utf8, utf8.length()));
  }

  return std::nullopt;
}

}  // end namespace MiniRacer
