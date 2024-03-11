#ifndef INCLUDE_MINI_RACER_ISOLATE_PUMP_H
#define INCLUDE_MINI_RACER_ISOLATE_PUMP_H

#include <v8.h>
#include <thread>

namespace MiniRacer {

/** Pumps the message bus for an isolate and schedules foreground tasks. */
class IsolatePump {
 public:
  IsolatePump(v8::Platform* platform, v8::Isolate* isolate);
  ~IsolatePump();

  void RunInForegroundRunner(std::function<void()> func);

 private:
  void PumpMessages();

  v8::Platform* platform_;
  v8::Isolate* isolate_;
  std::atomic<bool> shutdown_;
  std::thread message_pump_;
};

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_ISOLATE_PUMP_H
