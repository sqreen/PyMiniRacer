#ifndef INCLUDE_MINI_RACER_ISOLATE_HOLDER_H
#define INCLUDE_MINI_RACER_ISOLATE_HOLDER_H

#include <v8.h>

namespace MiniRacer {

/** Create and manage lifecycle of a v8::Isolate */
class IsolateHolder {
 public:
  IsolateHolder();
  ~IsolateHolder();

  v8::Isolate* Get() { return isolate_; }

 private:
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_HOLDER_H
