#include "isolate_manager.h"
#include <libplatform/libplatform.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-microtask-queue.h>
#include <v8-platform.h>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include "isolate_holder.h"

namespace MiniRacer {

IsolateManager::IsolateManager(v8::Platform* platform)
    : platform_(platform),
      tracker_(std::make_shared<IsolateManagerTracker>(platform)),
      isolate_([tracker = tracker_]() {
        auto thread = std::thread(&IsolateManager::PumpMessages, tracker);

        // Because the IsolateManager is managed by a std::shared_ptr, it is
        // destructed in less-than-predictable places which include callees of
        // this thread's message pump. Because ~IsolateManager may thus be
        // called by this thread, it's impossible to thread.join() it (a thread
        // can't join itself!). So instead we simply detach the thread, and
        // trust that it will clean itself up:
        thread.detach();

        // Blocks until the thread produces its isolate:
        return tracker->GetIsolate();
      }()) {}

IsolateManagerTracker::IsolateManagerTracker(v8::Platform* platform)
    : platform_(platform),
      shutdown_flag_(false),
      isolate_future_(isolate_promise_.get_future()) {}

auto IsolateManagerTracker::GetPlatform() -> v8::Platform* {
  return platform_;
}

auto IsolateManagerTracker::GetIsolate() -> v8::Isolate* {
  return isolate_future_.get();
}

void IsolateManagerTracker::SetIsolate(v8::Isolate* isolate) {
  isolate_promise_.set_value(isolate);
}

void IsolateManagerTracker::ShutDown(IsolateManager* isolate_manager) {
  const std::lock_guard<std::mutex> lock(mutex_);

  shutdown_flag_ = true;

  // A trick from v8/src/d8/d8.cc Worker::Terminate():
  // Throw a no-op task onto the queue just to kick the message loop into
  // noticing we're in shutdown mode:
  isolate_manager->Run([](v8::Isolate*) {});
}

auto IsolateManagerTracker::ShouldShutDown() -> bool {
  const std::lock_guard<std::mutex> lock(mutex_);
  return shutdown_flag_;
}

void IsolateManager::TerminateOngoingTask() {
  isolate_->TerminateExecution();
}

// We use a value param here to ensure the IsolateManagerTracker is alive
// through the life of this function call (which happens to be a thread main).
// NOLINTBEGIN(performance-unnecessary-value-param)
void IsolateManager::PumpMessages(
    std::shared_ptr<IsolateManagerTracker> tracker) {
  // Note that this function runs in a thread which will slightly outlive the
  // IsolateManager which spawned it. Thus this is a static method, and owns or
  // co-owns the object lifecycle for all data it uses.

  IsolateHolder isolate_holder;

  // By design, only this, the message pump thread, is ever allowed to touch
  // the isolate, so go ahead and lock it:
  const v8::Locker lock(isolate_holder.Get());
  const v8::Isolate::Scope scope(isolate_holder.Get());

  // However, some APIs, like posting and terminating tasks, don't require the
  // lock. For such tasks, expose the isolate pointer:
  tracker->SetIsolate(isolate_holder.Get());

  const v8::SealHandleScope shs(isolate_holder.Get());
  while (!tracker->ShouldShutDown()) {
    // Run message loop items (like timers)
    v8::platform::PumpMessageLoop(
        tracker->GetPlatform(), isolate_holder.Get(),
        v8::platform::MessageLoopBehavior::kWaitForWork);

    v8::MicrotasksScope::PerformCheckpoint(isolate_holder.Get());
  }
}
// NOLINTEND(performance-unnecessary-value-param)

void IsolateManager::RunInterrupt(v8::Isolate* /*isolate*/, void* data) {
  std::unique_ptr<v8::Task> task(static_cast<v8::Task*>(data));

  task->Run();
}

IsolateManager::~IsolateManager() {
  tracker_->ShutDown(this);
}

}  // end namespace MiniRacer
