#ifndef INCLUDE_MINI_RACER_CODE_EVALUATOR_H
#define INCLUDE_MINI_RACER_CODE_EVALUATOR_H

#include <v8.h>
#include "binary_value.h"
#include "isolate_memory_monitor.h"

namespace MiniRacer {

/** Parse and run arbitrary scripts within an isolate */
class CodeEvaluator {
 public:
  CodeEvaluator(v8::Isolate* isolate,
                BinaryValueFactory* bv_factory,
                IsolateMemoryMonitor* memory_monitor);
  ~CodeEvaluator();

  BinaryValue::Ptr Eval(const std::string& code, unsigned long timeout);

 private:
  std::optional<std::string> ValueToUtf8String(v8::Local<v8::Value> value);

  BinaryValue::Ptr SummarizeTryCatch(v8::Local<v8::Context>& context,
                                     const v8::TryCatch& trycatch,
                                     BinaryTypes resultType);

  v8::Isolate* isolate_;
  BinaryValueFactory* bv_factory_;
  IsolateMemoryMonitor* memory_monitor_;
  std::unique_ptr<v8::Persistent<v8::Context>> context_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CODE_EVALUATOR_H
