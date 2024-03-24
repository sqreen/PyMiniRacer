#include "isolate_holder.h"

#include <v8-array-buffer.h>
#include <v8-isolate.h>

namespace MiniRacer {

IsolateHolder::IsolateHolder()
    : allocator_(v8::ArrayBuffer::Allocator::NewDefaultAllocator()) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = allocator_.get();

  isolate_ = v8::Isolate::New(create_params);
}

IsolateHolder::~IsolateHolder() {
  isolate_->Dispose();
}

}  // end namespace MiniRacer
