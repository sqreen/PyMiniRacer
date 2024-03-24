#ifndef INCLUDE_MINI_RACER_ISOLATE_HOLDER_H
#define INCLUDE_MINI_RACER_ISOLATE_HOLDER_H

#include <v8-array-buffer.h>
#include <v8-isolate.h>
#include <memory>

namespace MiniRacer {

/** Create and manage lifecycle of a v8::Isolate */
class IsolateHolder {
 public:
  IsolateHolder();
  ~IsolateHolder();

  IsolateHolder(const IsolateHolder&) = delete;
  auto operator=(const IsolateHolder&) -> IsolateHolder& = delete;
  IsolateHolder(IsolateHolder&&) = delete;
  auto operator=(IsolateHolder&& other) -> IsolateHolder& = delete;

  auto Get() -> v8::Isolate*;

 private:
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  v8::Isolate* isolate_;
};

inline auto IsolateHolder::Get() -> v8::Isolate* {
  return isolate_;
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_HOLDER_H
