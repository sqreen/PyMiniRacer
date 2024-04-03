#include "isolate_manager.h"
#include <libplatform/libplatform.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-microtask-queue.h>
#include <v8-platform.h>
#include <memory>
#include <thread>

namespace MiniRacer {

IsolateManager::IsolateManager(v8::Platform* platform)
    : platform_(platform),
      task_runner_(platform->GetForegroundTaskRunner(isolate_holder_.Get())) {
  thread_ = std::thread(&IsolateManager::PumpMessages, this);
}

void IsolateManager::TerminateOngoingTask() {
  isolate_holder_.Get()->TerminateExecution();
}

void IsolateManager::PumpMessages() {
  // By the design of PyMiniRacer, only this, the message pump thread, is ever
  // allowed to touch the isolate, so go ahead and lock it:
  const v8::Locker lock(isolate_holder_.Get());

  const v8::SealHandleScope shs(isolate_holder_.Get());
  while (!shutdown_) {
    // Run message loop items (like timers)
    if (!v8::platform::PumpMessageLoop(
            platform_, isolate_holder_.Get(),
            v8::platform::MessageLoopBehavior::kWaitForWork)) {
      break;
    }

    v8::MicrotasksScope::PerformCheckpoint(isolate_holder_.Get());
  }
}

void IsolateManager::RunInterrupt(v8::Isolate* /*isolate*/, void* data) {
  std::unique_ptr<v8::Task> task(static_cast<v8::Task*>(data));

  task->Run();
}

void IsolateManager::Shutdown() {
  const bool was_shutdown = shutdown_.exchange(true);
  if (was_shutdown) {
    return;
  }

  // From v8/src/d8/d8.cc Worker::Terminate():
  // Throw a no-op task on the queue just to kick the message loop into noticing
  // we're in shutdown mode:
  Run([](v8::Isolate*) {});

  // From v8/src/d8/d8.cc Worker::Terminate():
  // Terminate any ongoing execution (in case some JS is running forever):
  // isolate_holder_.Get()->TerminateExecution();

  thread_.join();
}

IsolateManagerStopper::IsolateManagerStopper(IsolateManager* isolate_manager)
    : isolate_manager_(isolate_manager) {}

IsolateManagerStopper::~IsolateManagerStopper() {
  isolate_manager_->Shutdown();
}

}  // end namespace MiniRacer
