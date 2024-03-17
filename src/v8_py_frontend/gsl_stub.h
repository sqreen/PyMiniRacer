#ifndef GSL_STUB_H
#define GSL_STUB_H

// A stub for https://github.com/microsoft/GSL/blob/main/include/gsl/pointers
// To make clang-tidy static analysis happy.

namespace gsl {
template <typename T>
using owner = T;
}  // namespace gsl

#endif  // GSL_STUB_H
