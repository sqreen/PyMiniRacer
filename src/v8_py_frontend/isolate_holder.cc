#include "isolate_holder.h"

#include <v8-array-buffer.h>
#include <v8-isolate.h>
#include <v8-microtask.h>

namespace MiniRacer {

IsolateHolder::IsolateHolder()
    : allocator_(v8::ArrayBuffer::Allocator::NewDefaultAllocator()) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = allocator_.get();

  isolate_ = v8::Isolate::New(create_params);

  // We should set kExplicit since we're running the Microtasks checkpoint
  // manually in isolate_manager.cc. Per
  // https://stackoverflow.com/questions/54393127/v8-how-to-correctly-handle-microtasks
  isolate_->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
}

IsolateHolder::~IsolateHolder() {
  isolate_->Dispose();
}

}  // end namespace MiniRacer
