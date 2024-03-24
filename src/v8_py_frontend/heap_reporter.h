#ifndef INCLUDE_MINI_RACER_HEAP_REPORTER_H
#define INCLUDE_MINI_RACER_HEAP_REPORTER_H

#include <v8-isolate.h>
#include "binary_value.h"

namespace MiniRacer {

/** Report fun facts about an isolate heap */
class HeapReporter {
 public:
  HeapReporter(v8::Isolate* isolate, BinaryValueFactory* bv_factory);

  auto HeapSnapshot() -> BinaryValue::Ptr;
  auto HeapStats() -> BinaryValue::Ptr;

 private:
  v8::Isolate* isolate_;
  BinaryValueFactory* bv_factory_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_HEAP_REPORTER_H
