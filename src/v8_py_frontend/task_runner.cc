#include "task_runner.h"
#include <libplatform/libplatform.h>

namespace MiniRacer {

TaskRunner::TaskRunner(v8::Platform* platform, v8::Isolate* isolate)
    : platform_(platform), isolate_(isolate) {
  thread_ = std::thread(&TaskRunner::PumpMessages, this);
}

/** Just a silly way to run code on the foreground task runner thread. */
class AdHocTask : public v8::Task {
 public:
  explicit AdHocTask(std::function<void()> runnable)
      : runnable_(std::move(runnable)) {}

  void Run() override { runnable_(); }

 private:
  std::function<void()> runnable_;
};

void TaskRunner::Run(std::function<void()> func) {
  std::unique_ptr<v8::Task> task(new AdHocTask(std::move(func)));
  platform_->GetForegroundTaskRunner(isolate_)->PostTask(std::move(task));
}

void TaskRunner::PumpMessages() {
  v8::SealHandleScope shs(isolate_);
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
