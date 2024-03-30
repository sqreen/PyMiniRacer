#ifndef INCLUDE_MINI_RACER_CODE_EVALUATOR_H
#define INCLUDE_MINI_RACER_CODE_EVALUATOR_H

#include <v8-context.h>
#include <v8-exception.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
#include <atomic>
#include <cstdint>
#include <string>
#include "binary_value.h"
#include "isolate_memory_monitor.h"

namespace MiniRacer {

/** Parse and run arbitrary scripts within an isolate */
class CodeEvaluator {
 public:
  CodeEvaluator(v8::Persistent<v8::Context>* context,
                BinaryValueFactory* bv_factory,
                IsolateMemoryMonitor* memory_monitor);

  auto Eval(v8::Isolate* isolate, const std::string& code) -> BinaryValue::Ptr;

  [[nodiscard]] auto FunctionEvalCallCount() const -> uint64_t;
  [[nodiscard]] auto FullEvalCallCount() const -> uint64_t;

 private:
  auto SummarizeTryCatch(v8::Local<v8::Context>& context,
                         const v8::TryCatch& trycatch) -> BinaryValue::Ptr;

  static auto GetFunction(v8::Isolate* isolate,
                          const std::string& code,
                          v8::Local<v8::Context>& context,
                          v8::Local<v8::Function>* func) -> bool;
  auto EvalFunction(v8::Isolate* isolate,
                    const v8::Local<v8::Function>& func,
                    v8::Local<v8::Context>& context) -> BinaryValue::Ptr;
  auto EvalAsScript(v8::Isolate* isolate,
                    const std::string& code,
                    v8::Local<v8::Context>& context) -> BinaryValue::Ptr;

  v8::Persistent<v8::Context>* context_;
  BinaryValueFactory* bv_factory_;
  IsolateMemoryMonitor* memory_monitor_;
  std::atomic<uint64_t> function_eval_call_count_{0};
  std::atomic<uint64_t> full_eval_call_count_{0};
};

inline auto CodeEvaluator::FunctionEvalCallCount() const -> uint64_t {
  return function_eval_call_count_;
}

inline auto CodeEvaluator::FullEvalCallCount() const -> uint64_t {
  return full_eval_call_count_;
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CODE_EVALUATOR_H
