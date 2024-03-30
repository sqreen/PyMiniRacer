#include "task_runner.h"
#include <libplatform/libplatform.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-microtask-queue.h>
#include <v8-platform.h>
#include <thread>

namespace MiniRacer {

TaskRunner::TaskRunner(v8::Platform* platform, v8::Isolate* isolate)
    : platform_(platform), isolate_(isolate) {
  thread_ = std::thread(&TaskRunner::PumpMessages, this);
}

void TaskRunner::TerminateOngoingTask() {
  isolate_->TerminateExecution();
}

void TaskRunner::PumpMessages() {
  const v8::SealHandleScope shs(isolate_);
  while (!shutdown_) {
    // Run message loop items (like timers)
    if (!v8::platform::PumpMessageLoop(
            platform_, isolate_,
            v8::platform::MessageLoopBehavior::kWaitForWork)) {
      break;
    }

    if (!shutdown_) {
      v8::MicrotasksScope::PerformCheckpoint(isolate_);
    }
  }
}

TaskRunner::~TaskRunner() {
  shutdown_ = true;

  // From v8/src/d8/d8.cc Worker::Terminate():
  // Throw a no-op task on the queue just to kick the message loop into noticing
  // we're in shutdown mode:
  Run([]() {});

  // From v8/src/d8/d8.cc Worker::Terminate():
  // Terminate any ongoing execution (in case some JS is running forever):
  isolate_->TerminateExecution();

  thread_.join();
}

}  // end namespace MiniRacer
