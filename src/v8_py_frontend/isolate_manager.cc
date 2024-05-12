#include "isolate_manager.h"
#include <libplatform/libplatform.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-microtask-queue.h>
#include <v8-platform.h>
#include <future>
#include <memory>
#include <thread>
#include "isolate_holder.h"

namespace MiniRacer {

IsolateManager::IsolateManager(v8::Platform* platform)
    : platform_(platform),
      message_pump_(std::make_shared<IsolateMessagePump>(platform)),
      isolate_(IsolateMessagePump::Start(message_pump_)) {}

IsolateManager::~IsolateManager() {
  Run([message_pump = message_pump_](v8::Isolate*) {
    message_pump->ShutDown();
  });
}

void IsolateManager::TerminateOngoingTask() {
  isolate_->TerminateExecution();
}

IsolateMessagePump::IsolateMessagePump(v8::Platform* platform)
    : platform_(platform),
      shutdown_flag_(false),
      isolate_future_(isolate_promise_.get_future()) {}

auto IsolateMessagePump::Start(
    const std::shared_ptr<IsolateMessagePump>& message_pump) -> v8::Isolate* {
  // We intentionally copy the shared_ptr here so we hold onto a reference to
  // the IsolateMessagePump object for the life of the pump thread:
  auto thread = std::thread([pump = message_pump]() { pump->PumpMessages(); });

  // Because the IsolateManager is managed by a std::shared_ptr, it is
  // destructed in less-than-predictable places which include callees of
  // this thread's message pump. Because ~IsolateManager may thus be
  // called by this thread, it's impossible to thread.join() it (a thread
  // can't join itself!). So instead we simply detach the thread, and
  // trust that it will clean itself up:
  thread.detach();

  // Blocks until the thread produces its isolate:
  return message_pump->isolate_future_.get();
}

void IsolateMessagePump::ShutDown() {
  shutdown_flag_ = true;
}

void IsolateMessagePump::PumpMessages() {
  IsolateHolder isolate_holder;

  // By design, only this, the message pump thread, is ever allowed to touch
  // the isolate, so go ahead and lock it:
  const v8::Locker lock(isolate_holder.Get());
  const v8::Isolate::Scope scope(isolate_holder.Get());

  // However, some APIs, like posting and terminating tasks, don't require the
  // lock. For such APIs, expose the isolate pointer:
  isolate_promise_.set_value(isolate_holder.Get());

  const v8::SealHandleScope shs(isolate_holder.Get());
  while (!shutdown_flag_) {
    v8::platform::PumpMessageLoop(
        platform_, isolate_holder.Get(),
        v8::platform::MessageLoopBehavior::kWaitForWork);

    if (!shutdown_flag_) {
      v8::MicrotasksScope::PerformCheckpoint(isolate_holder.Get());
    }
  }

  // Drain the message queue. This is important because we may have memory
  // cleanup tasks on it:
  while (v8::platform::PumpMessageLoop(
      platform_, isolate_holder.Get(),
      v8::platform::MessageLoopBehavior::kDoNotWait)) {
  }
}

void IsolateManager::RunInterrupt(v8::Isolate* /*isolate*/, void* data) {
  std::unique_ptr<v8::Task> task(static_cast<v8::Task*>(data));

  task->Run();
}

}  // end namespace MiniRacer
