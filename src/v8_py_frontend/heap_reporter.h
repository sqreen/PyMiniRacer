#ifndef INCLUDE_MINI_RACER_HEAP_REPORTER_H
#define INCLUDE_MINI_RACER_HEAP_REPORTER_H

#include <v8-isolate.h>
#include "binary_value.h"

namespace MiniRacer {

/** Report fun facts about an isolate heap */
class HeapReporter {
 public:
  explicit HeapReporter(BinaryValueFactory* bv_factory);

  auto HeapSnapshot(v8::Isolate* isolate) -> BinaryValue::Ptr;
  auto HeapStats(v8::Isolate* isolate) -> BinaryValue::Ptr;

 private:
  BinaryValueFactory* bv_factory_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_HEAP_REPORTER_H
