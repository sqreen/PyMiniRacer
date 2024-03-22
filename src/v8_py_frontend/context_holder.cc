#include "context_holder.h"

namespace MiniRacer {

ContextHolder::ContextHolder(v8::Isolate* isolate) {
  v8::Locker lock(isolate);
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);

  context_ = std::make_unique<v8::Persistent<v8::Context>>(
      isolate, v8::Context::New(isolate));
}

ContextHolder::~ContextHolder() {
  context_->Reset();
}

}  // end namespace MiniRacer
