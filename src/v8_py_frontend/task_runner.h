#ifndef INCLUDE_MINI_RACER_TASK_RUNNER_H
#define INCLUDE_MINI_RACER_TASK_RUNNER_H

#include <v8-isolate.h>
#include <v8-platform.h>
#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace MiniRacer {

/** Pumps the message bus for an isolate and schedules foreground tasks. */
class TaskRunner {
 public:
  TaskRunner(v8::Platform* platform, v8::Isolate* isolate);
  ~TaskRunner();

  TaskRunner(const TaskRunner&) = delete;
  auto operator=(const TaskRunner&) -> TaskRunner& = delete;
  TaskRunner(TaskRunner&&) = delete;
  auto operator=(TaskRunner&& other) -> TaskRunner& = delete;

  template <typename Func>
  void Run(Func func);

  void TerminateOngoingTask();

 private:
  void PumpMessages();

  v8::Platform* platform_;
  v8::Isolate* isolate_;
  std::atomic<bool> shutdown_;
  std::thread thread_;
};

/** Just a silly way to run code on the foreground task runner thread. */
template <typename Func>
class AdHocTask : public v8::Task {
 public:
  explicit AdHocTask(Func runnable);

  void Run() override;

 private:
  Func runnable_;
};

template <typename Func>
inline void TaskRunner::Run(Func func) {
  platform_->GetForegroundTaskRunner(isolate_)->PostTask(
      std::make_unique<AdHocTask<Func>>(std::move(func)));
}

template <typename Func>
inline AdHocTask<Func>::AdHocTask(Func runnable)
    : runnable_(std::move(runnable)) {}

template <typename Func>
inline void AdHocTask<Func>::Run() {
  runnable_();
}

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_TASK_RUNNER_H
