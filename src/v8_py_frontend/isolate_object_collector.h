#ifndef INCLUDE_MINI_RACER_ISOLATE_OBJECT_COLLECTOR_H
#define INCLUDE_MINI_RACER_ISOLATE_OBJECT_COLLECTOR_H

#include <v8-isolate.h>
#include <functional>
#include <mutex>
#include <vector>
#include "gsl_stub.h"
#include "isolate_manager.h"

namespace MiniRacer {

/** Deletes v8 objects.
 *
 * Things that want to delete v8 objects often don't own the isolate lock
 * (i.e., aren't running from the IsolateManager's message loop). From the V8
 * documentation, it's not clear if we can safely free v8 objects like a
 * v8::Persistent handle or decrement the ref count of a
 * std::shared_ptr<v8::BackingStore> (which may free the BackingStore) without
 * the lock. As a rule, messing with v8::Isolate-owned objects without holding
 * the Isolate lock is not safe, and there is no documentation indicating
 * methods like v8::Persistent::~Persistent are exempt from this rule. So this
 * class delegates deletion to the Isolate message loop.
 */
class IsolateObjectCollector {
 public:
  explicit IsolateObjectCollector(IsolateManager* isolate_manager);

  template <typename T>
  void Collect(T* obj);

  void Dispose();

 private:
  IsolateManager* isolate_manager_;
  std::mutex mutex_;
  std::vector<std::function<void()>> garbage_;
};

/** A deleter for use with std::shared_ptr and std::unique_ptr. */
class IsolateObjectDeleter {
 public:
  IsolateObjectDeleter();
  explicit IsolateObjectDeleter(
      IsolateObjectCollector* isolate_object_collector);

  template <typename T>
  void operator()(T* handle) const;

 private:
  IsolateObjectCollector* isolate_object_collector_;
};

template <typename T>
inline void IsolateObjectCollector::Collect(gsl::owner<T*> obj) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    garbage_.push_back([obj]() { delete obj; });
  }

  // Note that we don't wait on the deletion of objects. Code needing to delete
  // objects may be called by Python, as a result of callbacks from the C++
  // side of MiniRacer. Those callbacks originate from the IsolateManager
  // message loop itself. If we were to wait on this *new* task we're adding to
  // the message loop, we would deadlock.
  isolate_manager_->Run([this](v8::Isolate*) { Dispose(); });
}

template <typename T>
void IsolateObjectDeleter::operator()(gsl::owner<T*> handle) const {
  isolate_object_collector_->Collect(handle);
}

}  // namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_OBJECT_COLLECTOR_H
