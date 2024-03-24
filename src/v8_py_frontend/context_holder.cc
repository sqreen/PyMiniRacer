#include "context_holder.h"

#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-persistent-handle.h>
#include <memory>

namespace MiniRacer {

ContextHolder::ContextHolder(v8::Isolate* isolate) {
  const v8::Locker lock(isolate);
  const v8::Isolate::Scope isolate_scope(isolate);
  const v8::HandleScope handle_scope(isolate);

  context_ = std::make_unique<v8::Persistent<v8::Context>>(
      isolate, v8::Context::New(isolate));
}

ContextHolder::~ContextHolder() {
  context_->Reset();
}

}  // end namespace MiniRacer
