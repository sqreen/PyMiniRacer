#include "code_evaluator.h"
#include "breaker_thread.h"

namespace MiniRacer {

CodeEvaluator::CodeEvaluator(v8::Isolate* isolate,
                             BinaryValueFactory* bv_factory,
                             IsolateMemoryMonitor* memory_monitor)
    : isolate_(isolate),
      bv_factory_(bv_factory),
      memory_monitor_(memory_monitor) {
  v8::Locker lock(isolate_);
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);

  context_.reset(
      new v8::Persistent<v8::Context>(isolate_, v8::Context::New(isolate_)));
}

CodeEvaluator::~CodeEvaluator() {
  context_->Reset();
}

namespace {
bool maybe_fast_call(const std::string& code) {
  // Does the code string end with '()'?
  // TODO check if the string is an identifier
  return (code.size() > 2 && code[code.size() - 2] == '(' &&
          code[code.size() - 1] == ')');
}
}  // end anonymous namespace

BinaryValue::Ptr CodeEvaluator::SummarizeTryCatch(
    v8::Local<v8::Context>& context,
    const v8::TryCatch& trycatch,
    BinaryTypes resultType) {
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

BinaryValue::Ptr CodeEvaluator::Eval(const std::string& code,
                                     unsigned long timeout) {
  v8::Locker lock(isolate_);
  v8::Isolate::Scope isolate__scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Context> context = context_->Get(isolate_);
  v8::Context::Scope context_scope(context);

  v8::TryCatch trycatch(isolate_);

  // Spawn a thread to inforce the timeout limit:
  BreakerThread breaker_thread(isolate_, timeout);

  bool parsed = false;

  // Is it a single function call?
  if (maybe_fast_call(code)) {
    v8::Local<v8::String> identifier;
    v8::Local<v8::Value> func;

    // Let's check if the value is a callable identifier
    parsed = v8::String::NewFromUtf8(isolate_, code.data(),
                                     v8::NewStringType::kNormal,
                                     static_cast<int>(code.size() - 2))
                 .ToLocal(&identifier) &&
             context->Global()->Get(context, identifier).ToLocal(&func) &&
             func->IsFunction();

    if (parsed) {
      // Call the identifier
      v8::MaybeLocal<v8::Value> maybe_value =
          v8::Local<v8::Function>::Cast(func)->Call(
              context, v8::Undefined(isolate_), 0, {});
      if (!maybe_value.IsEmpty()) {
        return bv_factory_->ConvertFromV8(context,
                                          maybe_value.ToLocalChecked());
      }
    }
  }

  // Fallback on a slower full Eval
  v8::Local<v8::String> asString;
  v8::Local<v8::Script> script;

  parsed =
      v8::String::NewFromUtf8(isolate_, code.data(), v8::NewStringType::kNormal,
                              static_cast<int>(code.size()))
          .ToLocal(&asString) &&
      v8::Script::Compile(context, asString).ToLocal(&script) &&
      !script.IsEmpty();

  if (!parsed) {
    return SummarizeTryCatch(context, trycatch, type_parse_exception);
  }

  v8::MaybeLocal<v8::Value> maybe_value = script->Run(context);
  if (!maybe_value.IsEmpty()) {
    return bv_factory_->ConvertFromV8(context, maybe_value.ToLocalChecked());
  }

  // Still didn't execute. Find an error:
  BinaryTypes resultType;

  if (memory_monitor_->IsHardMemoryLimitReached()) {
    resultType = type_oom_exception;
  } else if (breaker_thread.timed_out()) {
    resultType = type_timeout_exception;
  } else if (trycatch.HasTerminated()) {
    resultType = type_terminated_exception;
  } else {
    resultType = type_execute_exception;
  }

  return SummarizeTryCatch(context, trycatch, resultType);
}

std::optional<std::string> CodeEvaluator::ValueToUtf8String(
    v8::Local<v8::Value> value) {
  v8::String::Utf8Value utf8(isolate_, value);

  if (utf8.length()) {
    return std::make_optional(std::string(*utf8, utf8.length()));
  }

  return std::nullopt;
}

}  // end namespace MiniRacer
