#ifndef INCLUDE_MINI_RACER_CODE_EVALUATOR_H
#define INCLUDE_MINI_RACER_CODE_EVALUATOR_H

#include <v8-context.h>
#include <v8-exception.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
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

 private:
  v8::Persistent<v8::Context>* context_;
  BinaryValueFactory* bv_factory_;
  IsolateMemoryMonitor* memory_monitor_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CODE_EVALUATOR_H
