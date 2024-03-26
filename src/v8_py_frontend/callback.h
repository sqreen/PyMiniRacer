#ifndef INCLUDE_MINI_RACER_CALLBACK_H
#define INCLUDE_MINI_RACER_CALLBACK_H

#include "binary_value.h"

namespace MiniRacer {

using Callback = void (*)(void*, BinaryValue*);

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CALLBACK_H
