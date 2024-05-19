#include "isolate_manager.h"
#include <libplatform/libplatform.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-platform.h>
#include <thread>
#include <tuple>
#include "isolate_holder.h"

namespace MiniRacer {

IsolateManager::IsolateManager(v8::Platform* platform)
    : platform_(platform),
      state_(State::kRun),
      thread_([this]() { PumpMessages(); }) {}

IsolateManager::~IsolateManager() {
  ChangeState(State::kStop);
  thread_.join();
}

void IsolateManager::TerminateOngoingTask() {
  isolate_holder_.Get()->TerminateExecution();
}

void IsolateManager::StopJavaScript() {
  ChangeState(State::kNoJavaScript);
  TerminateOngoingTask();
}

void IsolateManager::PumpMessages() {
  // By design, only this, the message pump thread, is ever allowed to touch
  // the isolate, so go ahead and lock it:
  v8::Isolate* isolate = isolate_holder_.Get();
  const v8::Locker lock(isolate);
  const v8::Isolate::Scope scope(isolate);

  const v8::SealHandleScope shs(isolate);
  while (state_ == State::kRun) {
    v8::platform::PumpMessageLoop(
        platform_, isolate, v8::platform::MessageLoopBehavior::kWaitForWork);

    if (state_ == State::kRun) {
      isolate->PerformMicrotaskCheckpoint();
    }
  }

  const v8::Isolate::DisallowJavascriptExecutionScope disallow_js(
      isolate, v8::Isolate::DisallowJavascriptExecutionScope::OnFailure::
                   THROW_ON_FAILURE);
  while (state_ == State::kNoJavaScript) {
    v8::platform::PumpMessageLoop(
        platform_, isolate, v8::platform::MessageLoopBehavior::kWaitForWork);
  }
}

void IsolateManager::ChangeState(State state) {
  state_ = state;
  // Run a no-op task to kick the message loop into noticing we've switched
  // states:
  std::ignore = Run([](v8::Isolate*) {});
}

}  // end namespace MiniRacer
