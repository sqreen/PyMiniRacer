#ifndef INCLUDE_MINI_RACER_CALLBACK_H
#define INCLUDE_MINI_RACER_CALLBACK_H

#include <cstdint>
#include <functional>
#include "binary_value.h"

namespace MiniRacer {

using Callback = void (*)(uint64_t, BinaryValueHandle*);

using RememberValueAndCallback =
    std::function<void(uint64_t, BinaryValue::Ptr)>;

}  // end namespace MiniRacer

#endif  // INCLUDE_MINI_RACER_CALLBACK_H
