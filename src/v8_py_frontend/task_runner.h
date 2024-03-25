#ifndef INCLUDE_MINI_RACER_TASK_RUNNER_H
#define INCLUDE_MINI_RACER_TASK_RUNNER_H

#include <v8-isolate.h>
#include <v8-platform.h>
#include <atomic>
#include <functional>
#include <thread>

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

  void Run(std::function<void()> func);

  void TerminateOngoingTask();

 private:
  void PumpMessages();

  v8::Platform* platform_;
  v8::Isolate* isolate_;
  std::atomic<bool> shutdown_;
  std::thread thread_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_TASK_RUNNER_H
